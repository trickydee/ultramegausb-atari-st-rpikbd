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
#include "config.h"
#include "6301.h"
#include "cpu.h"
#include "util.h"
#include "tusb.h"
#include "HidInput.h"
#include "SerialPort.h"
#include "AtariSTMouse.h"
#include "UserInterface.h"
#include "xinput_host.h"  // Official tusb_xinput driver


#define ROMBASE     256
#define CYCLES_PER_LOOP 250  // Reduced from 1000 for better interrupt response (4x improvement)

extern unsigned char rom_HD6301V1ST_img[];
extern unsigned int rom_HD6301V1ST_img_len;


/**
 * Read bytes from the physical serial port and pass them to the HD6301
 * Read ALL available bytes to prevent command parameter delays
 */
static void handle_rx_from_st() {
    // Keep reading while bytes are available AND the 6301 can accept them
    // This ensures multi-byte commands (command + parameters) are received promptly
    unsigned char data;
    int bytes_received = 0;
    
    while (SerialPort::instance().recv(data)) {
        if (!hd6301_sci_busy()) {
            //printf("ST -> 6301 %X\n", data);
            hd6301_receive_byte(data);
            bytes_received++;
        } else {
            // 6301 RDR is full - stop and let ROM process the current byte
            // The UART FIFO will buffer this byte until next iteration
            //printf("WARNING: 6301 RDR busy, deferring byte 0x%02X\n", data);
            break;
        }
    }
    
    // Check for serial overrun errors (byte arrived while RDR was still full)
    // This would indicate the ROM firmware isn't reading bytes fast enough
    extern u_char iram[];  // Defined in ireg.c
    if (iram[0x11] & 0x40) {  // TRCSR register, ORFE bit (Overrun/Framing Error)
        static int overrun_count = 0;
        if ((++overrun_count % 100) == 1) {  // Don't spam, report every 100th
            printf("WARNING: Serial overrun error detected! (count: %d)\n", overrun_count);
        }
    }
}

/**
 * Prepare the HD6301 and load the ROM file
 */
void setup_hd6301() {
    BYTE* pram = hd6301_init();
    if (!pram) {
        printf("Failed to initialise HD6301\n");
        exit(-1);
    }
    memcpy(pram + ROMBASE, rom_HD6301V1ST_img, rom_HD6301V1ST_img_len);
}

void core1_entry() {
    // Initialise the HD6301
    setup_hd6301();
    hd6301_reset(1);

    unsigned long count = 0;
    absolute_time_t tm = get_absolute_time();
    while (true) {
        count += CYCLES_PER_LOOP;
        
        // Update the tx serial port status based on actual buffer state
        hd6301_tx_empty(SerialPort::instance().send_buf_empty() ? 1 : 0);

        hd6301_run_clocks(CYCLES_PER_LOOP);

        if ((count % 1000000) == 0) {
            //printf("Cycles = %lu\n", count);
            //printf("CPU cycles = %llu\n", cpu.ncycles);
        }

        tm = delayed_by_us(tm, CYCLES_PER_LOOP);
        sleep_until(tm);
    }
}


int main() {
    // Note: stdio_init_all() not called as it may interfere with SerialPort UART setup
    if (!tusb_init()) {
        // TinyUSB initialization failed
        // Can't print error since stdio not initialized
        return -1;
    }

    UserInterface ui;
    ui.init();
    ui.update();

    // Overclock the Pico for maximum performance
    if (!set_sys_clock_khz(DEFAULT_CPU_CLOCK_KHZ, false))
      printf("system clock %d MHz failed\n", DEFAULT_CPU_CLOCK_KHZ / 1000);
    else
      printf("system clock now %d MHz\n", DEFAULT_CPU_CLOCK_KHZ / 1000);

    // Setup the UART and HID instance.
    SerialPort::instance().open();
    SerialPort::instance().set_ui(ui);
    HidInput::instance().reset();
    HidInput::instance().set_ui(ui);

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
            HidInput::instance().handle_keyboard();
            HidInput::instance().handle_mouse(cpu.ncycles);
            HidInput::instance().handle_joystick();
            ui.update();
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
}

// XInput mount callback - called when Xbox controller is connected
void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, const xinputh_interface_t *xinput_itf) {
    printf("XINPUT MOUNTED: dev_addr=%d, instance=%d\n", dev_addr, instance);
    
    const char* type_str;
    switch (xinput_itf->type) {
        case XBOX360_WIRED:     type_str = "Xbox 360 Wired"; break;
        case XBOX360_WIRELESS:  type_str = "Xbox 360 Wireless"; break;
        case XBOXONE:           type_str = "Xbox One"; break;
        case XBOXOG:            type_str = "Xbox OG"; break;
        default:                type_str = "Unknown"; break;
    }
    printf("  Type: %s\n", type_str);
    
    // Register with Atari mapper
    xinput_register_controller(dev_addr, xinput_itf);
    
    // OLED message disabled - using persistent storage debug instead
    #if 0
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
    
    snprintf(line, sizeof(line), "A:%d I:%d", dev_addr, instance);
    ssd1306_draw_string(&disp, 15, 50, 1, line);
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
    printf("XINPUT UNMOUNTED: dev_addr=%d, instance=%d\n", dev_addr, instance);
    
    // Unregister from Atari mapper
    xinput_unregister_controller(dev_addr);
}

// XInput report callback - called when controller data is received
void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance, 
                                    xinputh_interface_t const* xid_itf, uint16_t len) {
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
