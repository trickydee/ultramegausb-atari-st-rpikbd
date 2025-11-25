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

#ifdef ENABLE_BLUEPAD32
// Use separate initialization file to avoid HID type conflicts between TinyUSB and btstack
#include "bluepad32_init.h"
#endif

// Forward declarations
extern "C" {
    void switch_check_delayed_init(void);
}

#define ROMBASE     256
#define CYCLES_PER_LOOP 100  // Ultra-low latency for timing-sensitive demos (10x improvement over original)

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

void __not_in_flash_func(core1_entry)() {
    // Wait for Core 0 to finish initializing UART0 and other peripherals
    // This is critical for XIP builds where initialization timing matters
    sleep_ms(200);
    
    // Initialise the HD6301
    setup_hd6301();
    
    hd6301_reset(1);
    // Note: RX queue is cleared at startup (automatically zero-initialized)
    unsigned long count = 0;
    absolute_time_t tm = get_absolute_time();
    while (true) {
        count += CYCLES_PER_LOOP;
        
        // Update the tx serial port status based on actual buffer state
        // Use C wrapper function that's in RAM for better performance
        hd6301_tx_empty(serial_send_buf_empty());

        hd6301_run_clocks(CYCLES_PER_LOOP);

        // Debug output removed for production
        // if ((count % 1000000) == 0) {
        //     char buf[128];
        //     snprintf(buf, sizeof(buf), "Core 1: Cycles = %lu, CPU cycles = %llu\n", count, cpu.ncycles);
        //     uart_puts(uart0, buf);
        // }

        tm = delayed_by_us(tm, CYCLES_PER_LOOP);
        sleep_until(tm);
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
    if (!tusb_init()) {
        // TinyUSB initialization failed
        // Can't print error since stdio not initialized
        return -1;
    }

    // Overclock the Pico for maximum performance
    // For Bluetooth builds, use lower clock speed for CYW43 stability
    // CYW43 chip has issues at high clock speeds (270MHz causes STALL timeouts)
    #ifdef ENABLE_BLUEPAD32
    uint32_t clock_khz = 150000;  // 150 MHz for Bluetooth stability
    printf("Bluetooth build: Using 150 MHz for CYW43 stability\n");
    #else
    uint32_t clock_khz = DEFAULT_CPU_CLOCK_KHZ;
    #endif
    
    if (!set_sys_clock_khz(clock_khz, false))
      printf("system clock %d MHz failed\n", clock_khz / 1000);
    else
      printf("system clock now %d MHz\n", clock_khz / 1000);

#ifdef ENABLE_BLUEPAD32
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

    absolute_time_t ten_ms = get_absolute_time();
    while (true) {
        absolute_time_t tm = get_absolute_time();

        // HIGH PRIORITY: Check for serial data from ST every loop iteration
        // At 7812 baud, bytes arrive every ~1.28ms - must not miss them!
        handle_rx_from_st();

        AtariSTMouse::instance().update();

        // 10ms handler for less time-critical tasks
        if (absolute_time_diff_us(ten_ms, tm) >= 10000) {
            ten_ms = tm;

            tuh_task();
            
#ifdef ENABLE_BLUEPAD32
            // Poll Bluepad32 async_context (non-blocking, must be called regularly)
            bluepad32_poll();
#endif
            
            // Check for Switch Pro Controller delayed initialization
            switch_check_delayed_init();
            
            HidInput::instance().handle_keyboard();
            HidInput::instance().handle_mouse(cpu.ncycles);
            HidInput::instance().handle_joystick();
#if ENABLE_OLED_DISPLAY
            ui.update();
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

// Global Xbox report counter for debugging (accessible from UI)
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
    
    // Increment joystick counter and update UI (FIX: Xbox controllers weren't incrementing counter)
    xinput_joy_count++;
    xinput_notify_ui_mount();
    
#if ENABLE_OLED_DISPLAY
    // Show XBOX splash screen (reinstated with debug info)
    extern ssd1306_t disp;
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 20, 10, 2, (char*)"XBOX!");
    
    char line[20];
    if (xinput_itf->type == XBOX360_WIRED) {
        ssd1306_draw_string(&disp, 15, 35, 1, (char*)"360 Wired");
    } else if (xinput_itf->type == XBOX360_WIRELESS) {
        ssd1306_draw_string(&disp, 10, 35, 1, (char*)"360 Wireless");
    } else if (xinput_itf->type == XBOXONE) {
        ssd1306_draw_string(&disp, 20, 35, 1, (char*)"Xbox One");
    } else {
        ssd1306_draw_string(&disp, 15, 35, 1, (char*)"Detected!");
    }
    
    // Show debug info: Address, Instance, Connection status
    snprintf(line, sizeof(line), "A:%d I:%d C:%d", dev_addr, instance, xinput_itf->connected);
    ssd1306_draw_string(&disp, 10, 50, 1, line);
    ssd1306_show(&disp);
    sleep_ms(3000);  // Extended to 3 seconds to read debug info
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
    
    // Decrement joystick counter and update UI (FIX: Xbox controllers weren't decrementing counter)
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
    
    // Debug disabled - using storage debug in xinput_atari.cpp instead
    #if 0
    static uint32_t report_count = 0;
    report_count++;
    
    if (report_count <= 5 && xid_itf->last_xfer_result == XFER_RESULT_SUCCESS && 
        xid_itf->connected && xid_itf->new_pad_data) {
        extern ssd1306_t disp;
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 15, 0, 1, (char*)"XBOX REPORT");
        
        char line[20];
        snprintf(line, sizeof(line), "Count: %lu", report_count);
        ssd1306_draw_string(&disp, 5, 15, 1, line);
        
        snprintf(line, sizeof(line), "Btns: %04X", xid_itf->pad.wButtons);
        ssd1306_draw_string(&disp, 5, 30, 1, line);
        
        snprintf(line, sizeof(line), "LX:%d LY:%d", xid_itf->pad.sThumbLX, xid_itf->pad.sThumbLY);
        ssd1306_draw_string(&disp, 5, 45, 1, line);
        
        ssd1306_show(&disp);
        sleep_ms(2000);
    }
    #endif
    
    tuh_xinput_receive_report(dev_addr, instance);
}
