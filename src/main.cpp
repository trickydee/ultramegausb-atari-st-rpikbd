/*
 * Atari ST RP2040 IKBD Emulator
 * Copyright (C) 2021 Roy Hopkins
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"  // For flash_safe_execute_core_init() - required for Bluetooth flash coordination
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "pico/stdio_uart.h"
#include "config.h"
#include "6301.h"
#include "cpu.h"
#include "util.h"
#include "tusb.h"
#include "HidInput.h"
#include "hardware/uart.h"  // For uart_puts on Core 1
#include "SerialPort.h"
#include "AtariSTMouse.h"
#include "UserInterface.h"
#include "xinput_host.h"  // Official tusb_xinput driver
#include "gamecube_adapter.h"  // GameCube adapter support

#if ENABLE_BLUEPAD32
// Use separate initialization file to avoid HID type conflicts between TinyUSB and btstack
#include "bluepad32_init.h"
#endif

#include "runtime_toggle.h"  // Runtime USB/Bluetooth toggle control

// Forward declarations
extern "C" {
    void switch_check_delayed_init(void);
}

#define ROMBASE     256
#define CYCLES_PER_LOOP 1000  // Match logronoid's value - proper 6301 emulation timing (~1MHz)

extern unsigned char rom_HD6301V1ST_img[];
extern unsigned int rom_HD6301V1ST_img_len;


/**
 * Software receive queue to buffer bytes when 6301 RDR is busy
 * Prevents byte loss during timing-critical periods
 */
#define RX_QUEUE_SIZE 32  // Buffer up to 32 bytes (matches UART FIFO size)
static unsigned char rx_queue[RX_QUEUE_SIZE];
static volatile int rx_queue_head = 0;
static volatile int rx_queue_tail = 0;
static volatile int rx_queue_count = 0;

/**
 * Clear the receive queue (called on reset to prevent stale data)
 */
static void clear_rx_queue() {
    rx_queue_head = 0;
    rx_queue_tail = 0;
    rx_queue_count = 0;
}

/**
 * Read bytes from the physical serial port and pass them to the HD6301
 * Uses software queue to buffer bytes when 6301 RDR is busy
 */
static void __not_in_flash_func(handle_rx_from_st)() {
    unsigned char data;
    static int bytes_deferred_count = 0;
    static int queue_full_count = 0;
    
    // First, try to drain the software queue (feed buffered bytes to 6301)
    while (rx_queue_count > 0 && !hd6301_sci_busy()) {
        unsigned char queued_byte = rx_queue[rx_queue_tail];
        hd6301_receive_byte(queued_byte);
        rx_queue_tail = (rx_queue_tail + 1) % RX_QUEUE_SIZE;
        rx_queue_count--;
    }
    
    // Now read new bytes from UART and queue them if 6301 is busy
    while (SerialPort::instance().recv(data)) {
        if (!hd6301_sci_busy()) {
            // 6301 RDR is available - send directly
            hd6301_receive_byte(data);
        } else {
            // 6301 RDR is busy - queue the byte
            if (rx_queue_count < RX_QUEUE_SIZE) {
                // Add to queue
                rx_queue[rx_queue_head] = data;
                rx_queue_head = (rx_queue_head + 1) % RX_QUEUE_SIZE;
                rx_queue_count++;
            } else {
                // Queue is full - this is very bad!
                queue_full_count++;
                if ((queue_full_count % 100) == 1) {
                    printf("CRITICAL: RX queue FULL! Byte 0x%02X LOST! (count: %d)\n", 
                           data, queue_full_count);
                }
                // Byte is lost - but at least we logged it
            }
            bytes_deferred_count++;
            if ((bytes_deferred_count % 1000) == 1) {
                printf("WARNING: 6301 RDR busy (deferred %d times, queue: %d/%d)\n", 
                       bytes_deferred_count, rx_queue_count, RX_QUEUE_SIZE);
            }
        }
    }
    
    // Check for serial overrun errors (byte arrived while RDR was still full)
    // This would indicate the ROM firmware isn't reading bytes fast enough
    extern u_char iram[];  // Defined in ireg.c
    if (iram[0x11] & 0x40) {  // TRCSR register, ORFE bit (Overrun/Framing Error)
        static int overrun_count = 0;
        if ((++overrun_count % 100) == 1) {  // Don't spam, report every 100th
            printf("WARNING: Serial overrun detected! ROM reading too slow. Count: %d\n", overrun_count);
            printf("  TRCSR=0x%02X (RDRF=%d ORFE=%d)\n", 
                   iram[0x11], 
                   (iram[0x11] & 0x80) ? 1 : 0,  // RDRF
                   (iram[0x11] & 0x40) ? 1 : 0); // ORFE
        }
        // Clear overrun flag to prevent continuous triggering
        // Note: ROM should also clear this when reading RDR
        iram[0x11] &= ~0x40;  // Clear ORFE bit
    }
}

/**
 * Prepare the HD6301 and load the ROM file
 */
void __not_in_flash_func(setup_hd6301)() {
    BYTE* pram = hd6301_init();
    if (!pram) {
        // Can't use printf/uart_puts reliably here if init failed
        // Just hang - this is a fatal error
        while(1) { tight_loop_contents(); }
    }
    
    // Copy ROM to RAM for XIP builds (improves performance)
    memcpy(pram + ROMBASE, rom_HD6301V1ST_img, rom_HD6301V1ST_img_len);
}

// Global flag to track Core 1 activity (updated by Core 1, read by Core 0)
static volatile uint32_t g_core1_heartbeat_counter = 0;
static volatile uint32_t g_core1_cycle_count = 0;
static volatile uint32_t g_core1_loop_counter = 0;  // Simple loop counter to detect if Core 1 is running
static volatile bool g_core1_paused = false;  // Flag to pause Core 1 during BT enumeration

// Functions to pause/resume Core 1 (called from BT callbacks)
extern "C" void core1_pause_for_bt_enumeration(void) {
    g_core1_paused = true;
}

extern "C" void core1_resume_after_bt_enumeration(void) {
    g_core1_paused = false;
}

void __not_in_flash_func(core1_entry)() {
    // CRITICAL: Initialize flash-safe execution FIRST
    // This allows Core 0 to coordinate with Core 1 when Bluetooth writes to flash (TLV storage)
    // Without this, Core 1 can freeze when Bluetooth tries to access flash
    // This matches logronoid's implementation and is required for proper flash coordination
    flash_safe_execute_core_init();
    
    // Wait for Core 0 to finish initializing UART0 and other peripherals
    // This is critical for XIP builds where initialization timing matters
    // Use busy_wait instead of sleep_ms to avoid timer dependency
    busy_wait_us(200000);  // 200ms delay
    
    // Initialise the HD6301
    setup_hd6301();
    
    hd6301_reset(1);
    // Note: RX queue is cleared at startup (automatically zero-initialized)
    unsigned long count = 0;
    
    // CRITICAL: Don't use printf here - it can block if UART0 is locked by Bluetooth
    // Instead, set a flag that Core 0 can check
    g_core1_heartbeat_counter = 0xFFFFFFFF;  // Magic value to indicate Core 1 started
    
    // Pure tight loop - NO DELAYS (matching logronoid's implementation)
    // This makes Core 1 completely independent of timer system and Core 0's USB/Bluetooth operations
    // Core 1 runs at maximum speed, only limited by CPU clock
    uint32_t loop_count = 0;
    uint32_t last_heartbeat_loop = 0;
    
    while (true) {
        // CRITICAL: Update loop counter FIRST to detect if Core 1 is frozen
        // This counter increments every loop, independent of emulator state
        g_core1_loop_counter++;  // Simple increment - if this stops, Core 1 is truly frozen
        
        // Check if Core 1 should be paused (during BT gamepad enumeration)
        if (g_core1_paused) {
            // Pause Core 1 to avoid flash access conflicts during BT enumeration
            // Use busy_wait_us for short delays to avoid timer dependency
            busy_wait_us(1000);  // 1ms delay - short enough to resume quickly
            continue;  // Skip emulation loop while paused
        }
        
        count += CYCLES_PER_LOOP;
        g_core1_cycle_count = count;  // Update global counter (non-blocking)
        
        // Update the tx serial port status based on actual buffer state
        // Use C wrapper function that's in RAM for better performance
        hd6301_tx_empty(serial_send_buf_empty());

        hd6301_run_clocks(CYCLES_PER_LOOP);

        loop_count++;
        
        // Heartbeat every ~5 seconds (approximately 50000 loops at 10kHz = 5 seconds)
        // Use loop counter instead of absolute_time to avoid Bluetooth blocking
        if (loop_count - last_heartbeat_loop >= 50000) {
            last_heartbeat_loop = loop_count;
            g_core1_heartbeat_counter++;  // Increment counter (non-blocking atomic operation)
        }

        // NO DELAY - pure tight loop (matching logronoid's approach)
        // Core 1 runs continuously without yielding, making it completely independent
        // of Core 0's USB/Bluetooth operations and timer interrupts
    }
}


int main() {
    // Bring up UART0 (GP0/GP1) for serial diagnostics without touching USB
    stdio_uart_init_full(uart0, 115200,
                         PICO_DEFAULT_UART_TX_PIN,
                         PICO_DEFAULT_UART_RX_PIN);
    uart_set_hw_flow(uart0, false, false);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uint actual_console_baud = uart_set_baudrate(uart0, 115200);
    printf("Console UART configured: requested 115200, actual %u baud\n", actual_console_baud);
    uart_puts(uart0, "UART0 console ready (115200 8N1)\r\n");

    // Note: stdio_init_all() not called as it may interfere with SerialPort UART setup
    // Initialize TinyUSB for USB HID device support (concurrent with Bluetooth)
    if (!tusb_init()) {
        // TinyUSB initialization failed
        printf("TinyUSB initialization failed\n");
        return -1;
    }
#if ENABLE_BLUEPAD32
    printf("USB initialized - Bluetooth + USB mode active\n");
#else
    printf("USB initialized - USB mode active\n");
#endif

    // Clock speed configuration
    // For Bluetooth builds, use 225MHz (matching logronoid's stable configuration)
    // CYW43 chip has issues at very high clock speeds (270MHz causes STALL timeouts)
    // 225MHz provides good balance between performance and stability
    #if ENABLE_BLUEPAD32
    uint32_t clock_khz = 225000;  // 225 MHz for Bluetooth builds (matching logronoid)
    printf("Bluetooth build: Using 225 MHz (matching logronoid's config)\n");
    #else
    uint32_t clock_khz = DEFAULT_CPU_CLOCK_KHZ;
    #endif
    
    if (!set_sys_clock_khz(clock_khz, false))
      printf("system clock %d MHz failed\n", clock_khz / 1000);
    else
      printf("system clock now %d MHz\n", clock_khz / 1000);

#if ENABLE_BLUEPAD32
    // CRITICAL: Initialize CYW43/Bluepad32 BEFORE any I2C/SPI peripherals
    // Forum reports show I2C/SPI initialization can interfere with CYW43 pin configuration
    // See: https://forums.pimoroni.com/t/plasma-2350-w-wifi-problems/26810
    if (!bluepad32_init()) {
        printf("Failed to initialize Bluepad32\n");
        return -1;
    }
    printf("Bluepad32 initialized - scanning for Bluetooth gamepads...\n");
#endif

#if ENABLE_OLED_DISPLAY
    // Initialize OLED display AFTER CYW43 to avoid pin conflicts
    // I2C initialization before CYW43 can put wireless pins in wrong state
    UserInterface ui;
    ui.init();
    ui.update();
#endif

    // Setup the UART and HID instance.
    SerialPort::instance().open();
#if ENABLE_OLED_DISPLAY
    SerialPort::instance().set_ui(ui);
#endif
    HidInput::instance().reset();
#if ENABLE_OLED_DISPLAY
    HidInput::instance().set_ui(ui);
#endif

    // The second CPU core is dedicated to the HD6301 emulation.
    multicore_launch_core1(core1_entry);

    // Force mouse enabled at startup
    HidInput::instance().force_usb_mouse();

    // Runtime state: USB and Bluetooth can be toggled on/off at runtime
    // Both start enabled by default (when ENABLE_BLUEPAD32 is defined, both are available)
    // State is managed by runtime_toggle functions
    printf("Runtime toggle available: USB and Bluetooth can be toggled at runtime\n");
#if ENABLE_BLUEPAD32
    // Enable Bluetooth by default if initialization succeeded
    if (bluepad32_is_enabled()) {
        bt_runtime_enable();  // Ensure Bluetooth is enabled at startup
        printf("Bluetooth enabled at startup\n");
    } else {
        bt_runtime_disable();
        printf("Bluetooth disabled (initialization failed)\n");
    }
    printf("Current state: USB=%s, BT=%s\n", 
           usb_runtime_is_enabled() ? "ON" : "OFF",
           bt_runtime_is_enabled() ? "ON" : "OFF");
#endif

    absolute_time_t ten_ms = get_absolute_time();
    absolute_time_t heartbeat_ms = get_absolute_time();
#if ENABLE_BLUEPAD32
    absolute_time_t bt_poll_ms = get_absolute_time();  // For Bluetooth polling (1ms interval)
    static uint32_t bt_poll_count = 0;
#endif
    static uint32_t loop_count = 0;
    
    printf("Main loop: Starting...\n");
    
    while (true) {
        absolute_time_t tm = get_absolute_time();
        loop_count++;

        // HIGH PRIORITY: Check for serial data from ST every loop iteration
        // At 7812 baud, bytes arrive every ~1.28ms - must not miss them!
        handle_rx_from_st();
        
        // Drain TX log buffer for UI display (non-critical path)
        SerialPort::instance().drain_tx_log();

        AtariSTMouse::instance().update();

        // 10ms handler for USB HID devices and other tasks (matching main branch approach)
        if (absolute_time_diff_us(ten_ms, tm) >= 10000) {
            ten_ms = tm;

            // USB task processing (moved from separate 5ms handler to 10ms handler)
            if (usb_runtime_is_enabled()) {
                tuh_task();
            }
            
            // Mouse handling MUST come before keyboard handling
            // This ensures wheel pulses are enqueued before they're consumed
            // Poll mouse if USB or Bluetooth is enabled (mouse can come from either)
#if ENABLE_BLUEPAD32
            if (usb_runtime_is_enabled() || bt_runtime_is_enabled()) {
                HidInput::instance().handle_mouse(cpu.ncycles);
            }
#else
            if (usb_runtime_is_enabled()) {
                HidInput::instance().handle_mouse(cpu.ncycles);
            }
#endif
            
            // Handle USB devices if USB is enabled
            if (usb_runtime_is_enabled()) {
                // Check for Switch Pro Controller delayed initialization
                switch_check_delayed_init();
                
                HidInput::instance().handle_keyboard();
            }
            
            // Handle Bluetooth keyboard/mouse if Bluetooth is enabled
            // (Bluetooth keyboard/mouse handling is integrated into handle_keyboard/handle_mouse)
#if ENABLE_BLUEPAD32
            if (bt_runtime_is_enabled()) {
                HidInput::instance().handle_keyboard();
            }
#endif
            
            // Joystick handler (handles GPIO, USB, and Bluetooth)
            HidInput::instance().handle_joystick();
            
#if ENABLE_BLUEPAD32
            // Check if Bluetooth UI update is needed (deferred from BT callbacks)
            // Only if Bluetooth is enabled
            if (bt_runtime_is_enabled()) {
                bluepad32_check_ui_update();
            }
#endif
            
#if ENABLE_OLED_DISPLAY
            ui.update();
#endif
        }
        
#if ENABLE_BLUEPAD32
        // Poll Bluetooth frequently (every 1ms) for responsive controller input
        // Matching logronoid's approach of frequent Bluetooth polling
        // Only poll if Bluetooth is enabled at runtime
        if (bt_runtime_is_enabled() && bluepad32_is_enabled() && absolute_time_diff_us(bt_poll_ms, tm) >= 1000) {
            bt_poll_ms = tm;
            bt_poll_count++;
            bluepad32_poll();  // Process Bluetooth events
            // Immediately poll USB after Bluetooth to prevent USB starvation (if USB enabled)
            if (usb_runtime_is_enabled()) {
                tuh_task();
            }
        }
#endif
        
        // Heartbeat: Print less frequently to reduce log spam but still show liveness
        // 10 seconds interval
        if (absolute_time_diff_us(heartbeat_ms, tm) >= 10000000) {
            heartbeat_ms = tm;
            uint32_t core1_heartbeat = g_core1_heartbeat_counter;
            uint32_t core1_cycles = g_core1_cycle_count;
            uint32_t core1_loops = g_core1_loop_counter;
            static uint32_t last_core1_cycles = 0;
            static uint32_t last_core1_loops = 0;
            bool core1_frozen = (core1_cycles == last_core1_cycles && core1_cycles > 0);
            bool core1_loops_frozen = (core1_loops == last_core1_loops && core1_loops > 0);
            last_core1_cycles = core1_cycles;
            last_core1_loops = core1_loops;
#if ENABLE_BLUEPAD32
            printf("Main loop: HEARTBEAT - loops=%lu, BT polls=%lu, Core1: hb=%lu cycles=%lu loops=%lu%s%s\n", 
                   loop_count, bt_poll_count, core1_heartbeat, core1_cycles, core1_loops,
                   core1_frozen ? " [CYCLES_FROZEN!]" : "",
                   core1_loops_frozen ? " [LOOPS_FROZEN!]" : "");
#else
            printf("Main loop: HEARTBEAT - loops=%lu, Core1: hb=%lu cycles=%lu loops=%lu%s%s\n", 
                   loop_count, core1_heartbeat, core1_cycles, core1_loops,
                   core1_frozen ? " [CYCLES_FROZEN!]" : "",
                   core1_loops_frozen ? " [LOOPS_FROZEN!]" : "");
#endif
        }
    }
    return 0;
}

//--------------------------------------------------------------------+
// TinyUSB XInput Driver Integration
//--------------------------------------------------------------------+

// Required by tusb_xinput library - register the XInput vendor driver
usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t* driver_count) {
    extern usbh_class_driver_t const usbh_xinput_driver;
    *driver_count = 1;
    return &usbh_xinput_driver;
}

// Forward declarations for Atari mapper
extern "C" {
    void xinput_register_controller(uint8_t dev_addr, const xinputh_interface_t* xid_itf);
    void xinput_unregister_controller(uint8_t dev_addr);
    
    // Xbox controller counter and UI notification functions (defined in HidInput.cpp)
    extern int xinput_joy_count;
    void xinput_notify_ui_mount();
    void xinput_notify_ui_unmount();
    
    // Switch Pro Controller delayed initialization
    void switch_check_delayed_init(void);
}

// Global Xbox report counter (accessible from UI)
static uint32_t xbox_report_count = 0;
extern "C" uint32_t get_xbox_report_count() {
    return xbox_report_count;
}

// XInput mount callback - called when Xbox controller is connected
void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, const xinputh_interface_t *xinput_itf) {
    const char* type_str;
    switch (xinput_itf->type) {
        case XBOX360_WIRED:     type_str = "Xbox 360 Wired"; break;
        case XBOX360_WIRELESS:  type_str = "Xbox 360 Wireless"; break;
        case XBOXONE:           type_str = "Xbox One"; break;
        case XBOXOG:            type_str = "Xbox OG"; break;
        default:                type_str = "Unknown"; break;
    }
    printf("Xbox controller mounted: %s (addr=%d, inst=%d)\n", type_str, dev_addr, instance);
    
    // Register with Atari mapper
    xinput_register_controller(dev_addr, xinput_itf);
    
    // Increment joystick counter and update UI
    xinput_joy_count++;
    xinput_notify_ui_mount();
    
#if ENABLE_OLED_DISPLAY
    extern ssd1306_t disp;
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 20, 10, 2, (char*)"XBOX!");
    
    if (xinput_itf->type == XBOX360_WIRED) {
        ssd1306_draw_string(&disp, 15, 35, 1, (char*)"360 Wired");
    } else if (xinput_itf->type == XBOX360_WIRELESS) {
        ssd1306_draw_string(&disp, 10, 35, 1, (char*)"360 Wireless");
    } else if (xinput_itf->type == XBOXONE) {
        ssd1306_draw_string(&disp, 20, 35, 1, (char*)"Xbox One");
    } else {
        ssd1306_draw_string(&disp, 15, 35, 1, (char*)"Detected!");
    }
    ssd1306_show(&disp);
    sleep_ms(2000);
#endif
    
    // For Xbox 360 Wireless, wait for connection before setting LEDs
    if (xinput_itf->type == XBOX360_WIRELESS && !xinput_itf->connected) {
        tuh_xinput_receive_report(dev_addr, instance);
        return;
    }
    
    // Set LED pattern and start receiving reports
    tuh_xinput_set_led(dev_addr, instance, 0, true);
    tuh_xinput_set_rumble(dev_addr, instance, 0, 0, true);
    tuh_xinput_receive_report(dev_addr, instance);
}

// XInput unmount callback
void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance) {
    printf("Xbox controller unmounted: addr=%d, inst=%d\n", dev_addr, instance);
    
    // Unregister from Atari mapper
    xinput_unregister_controller(dev_addr);
    
    // Decrement joystick counter and update UI
    if (xinput_joy_count > 0) {
        xinput_joy_count--;
    }
    xinput_notify_ui_unmount();
}

// XInput report callback - called when controller data is received
void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance, 
                                    xinputh_interface_t const* xid_itf, uint16_t len) {
    // Increment global counter for UI display
    xbox_report_count++;
    
    // Force new_pad_data flag to 1 (TinyUSB workaround)
    xinputh_interface_t* mutable_itf = (xinputh_interface_t*)xid_itf;
    mutable_itf->new_pad_data = 1;
    
    // Update controller state for mapper
    xinput_register_controller(dev_addr, xid_itf);
    
    tuh_xinput_receive_report(dev_addr, instance);
}
