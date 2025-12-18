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
#include "UserInterface.h"
#include "version.h"
#include "pico/stdlib.h"
#include "hid_app_host.h"
#include "config.h"
#include "hardware/clocks.h"
#if ENABLE_BLUEPAD32
#include "runtime_toggle.h"  // Runtime USB/Bluetooth toggle control
#endif

// Forward declare Xbox debug counters (defined in main.cpp and xinput_atari.cpp)
extern "C" {
    uint32_t get_xbox_report_count();
    uint32_t get_xbox_data_read_count();
    uint32_t get_xbox_lookup_calls();
    void get_xbox_debug_flags(uint8_t* addr, uint8_t* connected, uint8_t* new_data);
    uint32_t get_gpio_path_count();
    uint32_t get_usb_path_count();
    uint32_t get_hid_joy_success();
    uint32_t get_ps4_success();
    uint32_t get_xbox_success();
    uint32_t get_switch_success();
    void switch_get_debug_values(uint16_t* buttons, uint8_t* dpad, int16_t* lx, int16_t* ly,
                                  uint8_t* atari_dir, uint8_t* atari_fire);
    uint32_t switch_get_report_count(void);
    void switch_get_pro_init_status(bool* attempted, bool* complete, uint16_t* len_before, uint16_t* len_after);
    uint32_t switch_get_pro_init_elapsed(void);
    bool switch_get_pro_init_scheduled(void);
    void switch_get_raw_bytes(uint8_t* bytes, uint16_t* len);
    uint8_t switch_get_init_cmd_success(void);
}

#define DEBOUNCE_COUNT 10

enum BUTTONS {
    BUTTON_LEFT,
    BUTTON_MIDDLE,
    BUTTON_RIGHT
};

UserInterface::UserInterface() {
}

ssd1306_t   disp;

void UserInterface::init() {
    // Setup the I2C interface to the display
    i2c_init(SSD1306_I2C, 400000);
    gpio_set_function(SSD1306_SDA, GPIO_FUNC_I2C);
    gpio_set_function(SSD1306_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SSD1306_SDA);
    gpio_pull_up(SSD1306_SCL);

    // Initialise the display library
    ssd1306_init(&disp, SSD1306_WIDTH, SSD1306_HEIGHT, SSD1306_ADDR, SSD1306_I2C);

    // Setup GPIO for buttons
    btn_gpio[0] = GPIO_BUTTON_LEFT;
    btn_gpio[1] = GPIO_BUTTON_MIDDLE;
    btn_gpio[2] = GPIO_BUTTON_RIGHT;
    for (int i = 0; i < 3; ++i) {
        gpio_init(btn_gpio[i]);
        gpio_set_dir(btn_gpio[i], GPIO_IN);
        gpio_pull_up(btn_gpio[i]);
    }

    // Read from NV storage
    int mouse_speed = settings.get_settings().mouse_speed;
    if (mouse_speed < MOUSE_MIN) {
        mouse_speed = MOUSE_MIN;
        settings.get_settings().mouse_speed = mouse_speed;
    }
    if (mouse_speed > MOUSE_MAX) {
        mouse_speed = MOUSE_MAX;
        settings.get_settings().mouse_speed = mouse_speed;
    }

    serial_tm = get_absolute_time();
}

void UserInterface::usb_connect_state(int kb, int mouse, int joy) {
    if ((num_kb != kb) || (num_mouse != mouse) || (num_joy != joy)) {
        dirty = true;
    }
    num_kb = kb;
    num_mouse = mouse;
    num_joy = joy;
}

int8_t UserInterface::get_mouse_speed() {
    return settings.get_settings().mouse_speed;
}

uint8_t UserInterface::get_joystick() {
    return settings.get_settings().joy_device;
}

uint8_t UserInterface::get_mouse_enabled() {
    return settings.get_settings().mouse_enabled;
}

void UserInterface::set_mouse_enabled(uint8_t en) {
    settings.get_settings().mouse_enabled = en;
    settings.write();
    dirty = true;
}


void UserInterface::update_serial() {
    uint8_t y = 0;
    ssd1306_clear(&disp);
    for (auto it : serial_lines) {
        ssd1306_draw_string(&disp, 0, y, 1, (char*)it.c_str());
        y += 9;
    }
    ssd1306_draw_string(&disp, 24, 27, 1, (char*)"ST <-> Kbd");

	ssd1306_draw_string(&disp, 34, 0, 1, (char*)"V " PROJECT_VERSION_STRING);	    
}

void UserInterface::update_status() {
    char buf[32];
    // stdio_init_all();
    // Get the current CPU frequency
    uint32_t cpu_freq = clock_get_hz(clk_sys);
    //uint32_t cpu_freq = 123;
    ssd1306_clear(&disp);
    sprintf(buf, "%s %d", get_translation("USB Keyboard"), num_kb);
    ssd1306_draw_string(&disp, 0, 0, 1,  buf);
    sprintf(buf, "%s %d", get_translation("USB Mouse"), num_mouse);
    ssd1306_draw_string(&disp, 0, 9, 1,  buf);
    sprintf(buf, "%s %d", get_translation("USB Joystick"), num_joy);
    ssd1306_draw_string(&disp, 0, 18, 1, buf);
    sprintf(buf, "%s", get_translation(settings.get_settings().mouse_enabled ? "Mouse enabled" : "Joy 0 enabled"));
    ssd1306_draw_string(&disp, 0, 27, 1, buf);
    sprintf(buf, "CPU: %.2f MHz", static_cast<double>(cpu_freq) / 1000000.0);
    ssd1306_draw_string(&disp, 0, 36, 1, buf);
}

void UserInterface::update_mouse() {
    char buf[32];
    ssd1306_draw_string(&disp, 0, 45, 1, get_translation("Mouse speed"));
    sprintf(buf, "[==============]");
    buf[settings.get_settings().mouse_speed - MOUSE_MIN] = '*';
    ssd1306_draw_string(&disp, 0, 54, 1, buf);
}

void UserInterface::update_joy(int index) {
    char buf[32];
    sprintf(buf, "Joy %d: %s", index, (settings.get_settings().joy_device & (1 << index)) ? "DSub" : "USB");
    ssd1306_draw_string(&disp, 0, 54, 1, buf);
}

void UserInterface::update_splash() {
    ssd1306_clear(&disp);
    
    // ATARI text (centered)
    ssd1306_draw_string(&disp, 30, 0, 2, (char*)"ATARI");

    // Branding + version for ultramegausb.com
    ssd1306_draw_string(&disp, 4, 24, 1, (char*)"ultramegausb.com");
    // Move version up one line from the original bottom position
    ssd1306_draw_string(&disp, 40, 40, 1, (char*)"v" PROJECT_VERSION_STRING);
    
#if ENABLE_BLUEPAD32
    // Show USB/Bluetooth status on splash screen (bottom row)
    char mode_buf[32];
    bool usb_enabled = usb_runtime_is_enabled();
    bool bt_enabled = bt_runtime_is_enabled();
    
    if (usb_enabled && bt_enabled) {
        sprintf(mode_buf, "USB+BT");
    }
    else if (usb_enabled) {
        sprintf(mode_buf, "USB");
    }
    else if (bt_enabled) {
        sprintf(mode_buf, "BT");
    }
    else {
        sprintf(mode_buf, "OFF");
    }

    // Show mode on bottom row with label
    char mode_line[32];
    sprintf(mode_line, "Mode %s", mode_buf);
    ssd1306_draw_string(&disp, 0, 55, 1, mode_line);
#endif
}

void UserInterface::update_pro_init() {
    char buf[32];
    ssd1306_clear(&disp);
    
    // Get Pro Controller init status
    bool init_attempted, init_complete;
    uint16_t len_before, len_after;
    switch_get_pro_init_status(&init_attempted, &init_complete, &len_before, &len_after);
    
    // Get diagnostic info
    uint32_t elapsed_ms = switch_get_pro_init_elapsed();
    bool scheduled = switch_get_pro_init_scheduled();
    uint32_t report_count = switch_get_report_count();
    
    ssd1306_draw_string(&disp, 0, 0, 1, (char*)"Pro Init Status");
    
    if (!init_attempted) {
        // Not attempted yet - show diagnostic info
        sprintf(buf, "Elapsed: %lu ms", elapsed_ms);
        ssd1306_draw_string(&disp, 0, 15, 1, buf);
        
        sprintf(buf, "Scheduled: %s", scheduled ? "YES" : "NO");
        ssd1306_draw_string(&disp, 0, 27, 1, buf);
        
        sprintf(buf, "Reports: %lu", report_count);
        ssd1306_draw_string(&disp, 0, 39, 1, buf);
        
        if (elapsed_ms >= 1000 && scheduled) {
            ssd1306_draw_string(&disp, 0, 52, 1, (char*)"Should init!");
        } else if (!scheduled) {
            ssd1306_draw_string(&disp, 0, 52, 1, (char*)"Not scheduled?");
        } else {
            sprintf(buf, "Wait %lu ms", 1000 - elapsed_ms);
            ssd1306_draw_string(&disp, 0, 52, 1, buf);
        }
    } else if (!init_complete) {
        // Attempted but not complete yet
        ssd1306_draw_string(&disp, 0, 20, 1, (char*)"Init sent!");
        ssd1306_draw_string(&disp, 0, 35, 1, (char*)"Waiting for");
        ssd1306_draw_string(&disp, 0, 50, 1, (char*)"response...");
    } else {
        // Complete - show results
            sprintf(buf, "Before: %d bytes", len_before);
            ssd1306_draw_string(&disp, 0, 15, 1, buf);
            sprintf(buf, "After:  %d bytes", len_after);
            ssd1306_draw_string(&disp, 0, 25, 1, buf);
            
            // Show command success bitmask
            uint8_t cmd_mask = switch_get_init_cmd_success();
            sprintf(buf, "Cmds: 0x%02X/0x7F", cmd_mask);
            ssd1306_draw_string(&disp, 0, 37, 1, buf);
            
            if (len_after != len_before && len_before > 0) {
                if (cmd_mask == 0x7F) {
                    ssd1306_draw_string(&disp, 0, 52, 1, (char*)"All cmds OK!");
                } else {
                    sprintf(buf, "Some failed:%02X", cmd_mask);
                    ssd1306_draw_string(&disp, 0, 52, 1, buf);
                }
            } else {
                ssd1306_draw_string(&disp, 0, 52, 1, (char*)"NO CHANGE");
            }
        }
    }

void UserInterface::update_usb_debug() {
    char buf[32];
    ssd1306_clear(&disp);
    
#if ENABLE_CONTROLLER_DEBUG
    // Debug page with live controller diagnostics
    uint32_t switch_count = get_switch_success();
    
    // If Switch controller active, show live values
    if (switch_count > 0) {
        // Check if Pro Controller initialization completed
        bool init_attempted, init_complete;
        uint16_t len_before, len_after;
        switch_get_pro_init_status(&init_attempted, &init_complete, &len_before, &len_after);
        
        uint32_t rpt_count = switch_get_report_count();
        sprintf(buf, "SW Rpt:%lu Len:%d", rpt_count, len_after);
        ssd1306_draw_string(&disp, 0, 0, 1, buf);
        
        // Get live Switch values
        uint16_t btns;
        uint8_t dpad;
        int16_t lx, ly;
        uint8_t atari_dir, atari_fire;
        switch_get_debug_values(&btns, &dpad, &lx, &ly, &atari_dir, &atari_fire);
        
        sprintf(buf, "B:0x%04X DP:%d", btns, dpad);
        ssd1306_draw_string(&disp, 0, 10, 1, buf);
        
        sprintf(buf, "LX:%d LY:%d", lx, ly);
        ssd1306_draw_string(&disp, 0, 20, 1, buf);
        
        // Get raw bytes for mode 0x30 debugging
        uint8_t raw[9];
        uint16_t raw_len;
        switch_get_raw_bytes(raw, &raw_len);
        
        // Show raw button bytes (3-5) for mode 0x30
        if (raw_len >= 49) {
            sprintf(buf, "B3-5:%02X %02X %02X", raw[0], raw[1], raw[2]);
            ssd1306_draw_string(&disp, 0, 30, 1, buf);
            
            sprintf(buf, "LStk:%02X %02X %02X", raw[3], raw[4], raw[5]);
            ssd1306_draw_string(&disp, 0, 40, 1, buf);
        } else {
            sprintf(buf, "->D:0x%02X F:%d", atari_dir, atari_fire);
            ssd1306_draw_string(&disp, 0, 30, 1, buf);
            
            sprintf(buf, "UseCnt:%lu", switch_count);
            ssd1306_draw_string(&disp, 0, 40, 1, buf);
        }
        
        // Show if values are changing
        static uint16_t last_btns_displayed = 0xFFFF;
        static int16_t last_lx_displayed = 999;
        static int16_t last_ly_displayed = 999;
        static uint8_t last_raw3 = 0xFF;
        if (btns != last_btns_displayed || lx != last_lx_displayed || ly != last_ly_displayed || raw[0] != last_raw3) {
            sprintf(buf, "CHANGE!");
            last_btns_displayed = btns;
            last_lx_displayed = lx;
            last_ly_displayed = ly;
            last_raw3 = raw[0];
        } else {
            sprintf(buf, "STATIC");
        }
        ssd1306_draw_string(&disp, 0, 50, 1, buf);
    } else {
        // Standard debug page when no Switch active
        // Path counters at top
        uint32_t gpio_count = get_gpio_path_count();
        uint32_t usb_count = get_usb_path_count();
        sprintf(buf, "GPIO:%lu USB:%lu", gpio_count, usb_count);
        ssd1306_draw_string(&disp, 0, 0, 1, buf);
        
        // Device counts
        sprintf(buf, "KB:%d M:%d J:%d", num_kb, num_mouse, num_joy);
        ssd1306_draw_string(&disp, 0, 10, 1, buf);
        
        // Controller source counters
        uint32_t hid_count = get_hid_joy_success();
        uint32_t ps4_count = get_ps4_success();
        uint32_t xbox_count = get_xbox_success();
        
        sprintf(buf, "HID:%lu PS4:%lu", hid_count, ps4_count);
        ssd1306_draw_string(&disp, 0, 20, 1, buf);
        
        sprintf(buf, "SW:%lu Xbox:%lu", switch_count, xbox_count);
        ssd1306_draw_string(&disp, 0, 30, 1, buf);
        
        // Xbox report reception
        uint32_t rx_count = get_xbox_report_count();
        sprintf(buf, "XRx:%lu", rx_count);
        ssd1306_draw_string(&disp, 0, 40, 1, buf);
    }
#else
    // Simple USB status page (set ENABLE_CONTROLLER_DEBUG=0 in config.h)
    ssd1306_draw_string(&disp, 0, 0, 1, (char*)"USB Debug Info");
    
    // Device counts
    sprintf(buf, "KB:%d Mouse:%d Joy:%d", num_kb, num_mouse, num_joy);
    ssd1306_draw_string(&disp, 0, 12, 1, buf);
    
    // Mount and report stats
    sprintf(buf, "Mounts:%lu Active:%lu", 
        hid_debug_get_mount_calls(),
        hid_debug_get_active_devices());
    ssd1306_draw_string(&disp, 0, 24, 1, buf);
    
    sprintf(buf, "Reports:%lu", hid_debug_get_report_calls());
    ssd1306_draw_string(&disp, 0, 36, 1, buf);
#endif
}

void UserInterface::handle_buttons() {
    for (int i = 0; i < 3; ++i) {
        bool state = gpio_get(btn_gpio[i]);
        if (!state) {
            // The <= means we go one past DEBOUNCE_COUNT and latch there until the button is released.
            if (btn_count[i] <= DEBOUNCE_COUNT) {
                if (++btn_count[i] == DEBOUNCE_COUNT) {
                    on_button_down(i);
                }
            }
        }
        else {
            btn_count[i] = 0;
        }
    }
}

void UserInterface::toggle_joystick_source(uint8_t joystick_num) {
    if (joystick_num > 1) return;  // Only support Joy0 and Joy1
    
    // Toggle the joystick device bit (D-SUB <-> USB)
    settings.get_settings().joy_device ^= (1 << joystick_num);
    settings.write();
    dirty = true;
}

void UserInterface::on_button_down(int i) {
    // Middle button changes page
    if (i == BUTTON_MIDDLE) {
        int pg = (int)page;
#if ENABLE_SERIAL_LOGGING
    #if ENABLE_CONTROLLER_DEBUG
        // Debug mode (standard build): include all pages up to PRO_INIT
        pg = ((pg + 1) % (PAGE_PRO_INIT + 1));
    #else
        // Standard build without controller debug: cycle up to USB_DEBUG / SERIAL
        pg = ((pg + 1) % (PAGE_USB_DEBUG));
    #endif
#else
        // Production / speed builds (logging disabled): only cycle core pages
        // PAGE enum: SPLASH, MOUSE, JOY0, JOY1, SERIAL, ...
        // Limit cycle to 0..3 (SPLASH, MOUSE, JOY0, JOY1)
        pg = ((pg + 1) % PAGE_SERIAL);
#endif
        page = (PAGE)pg;
        dirty = true;
    }
    else if (i == BUTTON_LEFT) {
        if (page == PAGE_SPLASH) {
            // On ATARI splash screen, toggle USB/Bluetooth modes
#if ENABLE_BLUEPAD32
            // Cycle through: Both -> USB only -> BT only -> Both
            bool usb_enabled = usb_runtime_is_enabled();
            bool bt_enabled = bt_runtime_is_enabled();
            
            if (usb_enabled && bt_enabled) {
                // Both enabled -> USB only
                bt_runtime_disable();
                printf("Toggled to USB only mode\n");
            }
            else if (usb_enabled && !bt_enabled) {
                // USB only -> BT only
                usb_runtime_disable();
                bt_runtime_enable();
                printf("Toggled to Bluetooth only mode\n");
            }
            else if (!usb_enabled && bt_enabled) {
                // BT only -> Both
                usb_runtime_enable();
                printf("Toggled to USB + Bluetooth mode\n");
            }
            else {
                // Both disabled -> Both enabled (shouldn't happen, but handle it)
                usb_runtime_enable();
                bt_runtime_enable();
                printf("Toggled to USB + Bluetooth mode\n");
            }
            dirty = true;
#else
            // No Bluetooth support - just toggle USB (though this shouldn't be useful)
            printf("Bluetooth not available in this build\n");
#endif
        }
        else if (page == PAGE_MOUSE) {
            if (settings.get_settings().mouse_speed > MOUSE_MIN) {
                --settings.get_settings().mouse_speed;
                settings.write();
                dirty = true;
            }
        }
        else if ((page == PAGE_JOY0) || (page == PAGE_JOY1)) {
            settings.get_settings().joy_device ^= (1 << (page - PAGE_JOY0));
            settings.write();
            dirty = true;
        }
    }
    else if (i == BUTTON_RIGHT) {
        if (page == PAGE_MOUSE) {
            if (settings.get_settings().mouse_speed < MOUSE_MAX) {
                ++settings.get_settings().mouse_speed;
                settings.write();
                dirty = true;
            }
        }
        else if ((page == PAGE_JOY0) || (page == PAGE_JOY1)) {
            settings.get_settings().joy_device ^= (1 << (page - PAGE_JOY0));
            settings.write();
            dirty = true;
        }
    }
}

void UserInterface::update() {
    handle_buttons();

    if (dirty) {
        dirty = false;

        if (page == PAGE_MOUSE) {
            update_status();
            update_mouse();
        }
        else if (page == PAGE_JOY0) {
            update_status();
            update_joy(0);
        }
        else if (page == PAGE_JOY1) {
            update_status();
            update_joy(1);
        }
        else if (page == PAGE_SERIAL) {
#if ENABLE_SERIAL_LOGGING
            absolute_time_t tm = get_absolute_time();
            if (absolute_time_diff_us(serial_tm, tm) >= (500 * 1000)) {
                serial_tm = tm;
                update_serial();
            }
            else {
                // Not time yet
                dirty = true;
            }
#endif
        }
        else if (page == PAGE_SPLASH) {
            update_splash();
        }
        else if (page == PAGE_USB_DEBUG) {
#if ENABLE_CONTROLLER_DEBUG && ENABLE_SERIAL_LOGGING
            update_usb_debug();
            ssd1306_show(&disp);
            // Keep refreshing debug page
            dirty = true;
#endif
        }
        else if (page == PAGE_PRO_INIT) {
#if ENABLE_CONTROLLER_DEBUG && ENABLE_SERIAL_LOGGING
            update_pro_init();
            ssd1306_show(&disp);
            // Keep refreshing to show latest status
            dirty = true;
#endif
        }
        if (!dirty) {
            ssd1306_show(&disp);
        }
    }
}

void UserInterface::serial(bool send, uint8_t data) {
    char buf[32];
    sprintf(buf, "%s%02X", send ? "              " : "", data);

    serial_lines.push_back(std::string(buf));
    while (serial_lines.size() > 7) {
        serial_lines.pop_front();
    }
    if (page == PAGE_SERIAL) {
        dirty = true;
    }
}
