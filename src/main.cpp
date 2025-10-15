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


#define ROMBASE     256
#define CYCLES_PER_LOOP 1000

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
    while (SerialPort::instance().recv(data)) {
        if (!hd6301_sci_busy()) {
            //printf("ST -> 6301 %X\n", data);
            hd6301_receive_byte(data);
        } else {
            // 6301 RDR is full - stop and let ROM process the current byte
            // The UART FIFO will buffer this byte until next iteration
            break;
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
        // Update the tx serial port status based on our serial port handler
        hd6301_tx_empty(1);

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
