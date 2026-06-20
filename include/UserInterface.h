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
#pragma once

#include "ssd1306.h"
#include "NVSettings.h"
#include "mount_splash.h"
#include <string>
#include <deque>

#define MOUSE_MIN -7
#define MOUSE_MAX 8

extern const char* get_translation(const char* key);

class UserInterface {
public:
    UserInterface();

    enum PAGE {
        PAGE_SPLASH,
        PAGE_DEVICES,
        PAGE_MAPPING,
        PAGE_SERIAL,
        PAGE_USB_DEBUG,
        PAGE_PRO_INIT
    };

    void init();

    /**
     * Update USB and Bluetooth device counts for the UI
     */
    void device_connect_state(int usb_kb, int usb_mouse, int usb_joy,
                              int bt_kb, int bt_mouse, int bt_joy);

    /** @deprecated Use device_connect_state() */
    void usb_connect_state(int kb, int mouse, int joy);

    /**
     * Get the user specified mouse speed.
     * 0 = standard. -ve slower, +ve faster.
     */
    int8_t get_mouse_speed();

    /**
     * Get the joystick hardware assignment.
     * Bitfield where 1 = DSub joystick, 0 = USB.
     * Bit0 = Joystick 0
     * Bit1 = Joystick 1
     */
    uint8_t get_joystick();

    /**
     * Returns true if mouse enabled, false if joystick 0 enabled
     */
    uint8_t get_mouse_enabled();
    void set_mouse_enabled(uint8_t en);
    
    /**
     * Toggle joystick source between D-SUB and USB
     * @param joystick_num 0 or 1
     */
    void toggle_joystick_source(uint8_t joystick_num);

    /**
     * Update the display if necessary
     */
    void update();

    /** Request OLED redraw (e.g. map labels changed without count change). */
    void invalidate() { dirty = true; }

    /**
     * Serial transmission for logging to screen
     */
    void serial(bool send, uint8_t data);

private:
    void update_serial();
    void update_devices();
    void update_mapping();
    void update_usb_debug();
    void update_pro_init();
    void update_splash();
    void handle_buttons();
    void on_button_down(int i);

private:
    PAGE        page = PAGE_SPLASH;
    NVSettings  settings;
    bool        dirty = true;
    int         usb_kb = 0;
    int         usb_mouse = 0;
    int         usb_joy = 0;
    int         bt_kb = 0;
    int         bt_mouse = 0;
    int         bt_joy = 0;
    std::deque<std::string> serial_lines;
    absolute_time_t serial_tm;
    uint        btn_gpio[3];
    int         btn_count[3];
};

