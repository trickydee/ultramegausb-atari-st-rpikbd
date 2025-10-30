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
#include "HidInput.h"
#include "st_key_lookup.h"
#include "AtariSTMouse.h"
#include "tusb.h"
#include "hid_app_host.h"
#include "config.h"
#include "hardware/clocks.h"
#include "6301.h"
#include "ssd1306.h"
// xinput.h removed - using official xinput_host.h driver now
#include "ps4_controller.h"
#include "switch_controller.h"
#include "stadia_controller.h"
#include <map>

extern ssd1306_t disp;  // External reference to display

// Mouse toggle key is set to Ctrl+F12
#define TOGGLE_MOUSE_MODE 0x45  // F12 key (69 decimal)

// Ctrl+F8 sends 0x14 (SET JOYSTICK EVENT REPORTING)
#define RESTORE_JOYSTICK_KEY 0x41  // F8 key (65 decimal)

// Ctrl+F11 triggers XRESET
#define XRESET_KEY 0x44  // F11 key (68 decimal)

// Alt + / sends Atari INSERT
#define HID_KEY_SLASH 0x38  // Forward slash key (56 decimal)
#define ATARI_INSERT  82    // Atari ST INSERT scancode

// Alt + Plus/Minus for clock speed control
#define HID_KEY_EQUAL 0x2E  // = key (also + with shift) (46 decimal)
#define HID_KEY_MINUS 0x2D  // - key (45 decimal)

#define ATARI_LSHIFT 42
#define ATARI_RSHIFT 54
#define ATARI_ALT    56
#define ATARI_CTRL   29

#define GET_I32_VALUE(item)     (int32_t)(item->Value | ((item->Value & (1 << (item->Attributes.BitSize-1))) ? ~((1 << item->Attributes.BitSize) - 1) : 0))
#define JOY_GPIO_INIT(io)       gpio_init(io); gpio_set_dir(io, GPIO_IN); gpio_pull_up(io);

static std::map<int, uint8_t*> device;
static UserInterface* ui_ = nullptr;
static int kb_count = 0;
static int mouse_count = 0;
static int joy_count = 0;  // HID joysticks (not Xbox)

// Debug: Path counters (file-level static, accessed via getters)
static uint32_t g_gpio_path_count = 0;
static uint32_t g_usb_path_count = 0;
static uint32_t g_hid_joy_success = 0;
static uint32_t g_ps4_success = 0;
static uint32_t g_xbox_success = 0;
static uint32_t g_switch_success = 0;


extern "C" {

// Xbox controller counter (extern, modified by main.cpp xinput callbacks)
int xinput_joy_count = 0;

// Getters for path counters
uint32_t get_gpio_path_count() { return g_gpio_path_count; }
uint32_t get_usb_path_count() { return g_usb_path_count; }
uint32_t get_hid_joy_success() { return g_hid_joy_success; }
uint32_t get_ps4_success() { return g_ps4_success; }
uint32_t get_xbox_success() { return g_xbox_success; }
uint32_t get_switch_success() { return g_switch_success; }

// Functions for Xbox controller to notify UI of mount/unmount
void xinput_notify_ui_mount() {
    if (ui_) {
        // Total joysticks = HID joysticks + Xbox controllers
        ui_->usb_connect_state(kb_count, mouse_count, joy_count + xinput_joy_count);
    }
}

void xinput_notify_ui_unmount() {
    if (ui_) {
        // Total joysticks = HID joysticks + Xbox controllers
        ui_->usb_connect_state(kb_count, mouse_count, joy_count + xinput_joy_count);
    }
}

void tuh_hid_mounted_cb(uint8_t dev_addr) {
    // Decode mouse marker: if bit 7 is set, this is a mouse on a multi-interface device
    bool is_marked_mouse = (dev_addr & 0x80) != 0;
    uint8_t actual_addr = dev_addr & 0x7F;  // Strip marker bit
    
    HID_TYPE tp;
    if (is_marked_mouse) {
        tp = HID_MOUSE;  // Force MOUSE type
    } else {
        tp = tuh_hid_get_type(actual_addr);
    }
    
    // Debug disabled for performance
    #if 0
    static uint32_t mount_count = 0;
    mount_count++;
    extern ssd1306_t disp;
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 10, 0, 1, (char*)"MOUNT CALLBACK");
    
    char line1[20];
    const char* type_str = (tp == HID_MOUSE) ? "MOUSE" : 
                           (tp == HID_KEYBOARD) ? "KEYBOARD" : 
                           (tp == HID_JOYSTICK) ? "JOYSTICK" : "UNKNOWN";
    snprintf(line1, sizeof(line1), "A:%d(%d) T:%s", dev_addr, actual_addr, type_str);
    ssd1306_draw_string(&disp, 5, 15, 1, line1);
    
    char line2[20];
    snprintf(line2, sizeof(line2), "#%lu", mount_count);
    ssd1306_draw_string(&disp, 5, 30, 1, line2);
    
    char line3[20];
    snprintf(line3, sizeof(line3), "KB:%d M:%d J:%d", kb_count, mouse_count, joy_count);
    ssd1306_draw_string(&disp, 5, 45, 1, line3);
    
    ssd1306_show(&disp);
    sleep_ms(2000);
    #endif
    
    if (tp == HID_KEYBOARD) {
        // For keyboards, check if already registered (prevent multi-interface conflict)
        if (device.find(actual_addr) == device.end()) {
            device[actual_addr] = new uint8_t[sizeof(hid_keyboard_report_t)];
            hid_app_request_report(actual_addr, device[actual_addr]);
            ++kb_count;
        }
    }
    else if (tp == HID_MOUSE) {
        // For mice, always use actual address (same as keyboard on Logitech Unifying)
        // If keyboard already registered, skip - we'll handle mouse separately
        if (device.find(actual_addr) == device.end()) {
            device[actual_addr] = new uint8_t[tuh_hid_get_report_size(actual_addr)];
            hid_app_request_report(actual_addr, device[actual_addr]);
            ++mouse_count;
        } else {
            // Address already used - this is a multi-interface device
            // Add mouse with offset address
            int mouse_key = actual_addr + 128;
            device[mouse_key] = new uint8_t[tuh_hid_get_report_size(actual_addr)];
            // Use mouse_key here so find_device() finds the MOUSE device, not keyboard
            hid_app_request_report(mouse_key, device[mouse_key]);
            ++mouse_count;
        }
    }
    else if (tp == HID_JOYSTICK) {
        device[actual_addr] = new uint8_t[tuh_hid_get_report_size(actual_addr)];
        hid_app_request_report(actual_addr, device[actual_addr]);
        ++joy_count;
        
        // Check if this is a Stadia controller and show splash screen
        uint16_t vid, pid;
        tuh_vid_pid_get(actual_addr, &vid, &pid);
        extern bool stadia_is_controller(uint16_t vid, uint16_t pid);
        if (stadia_is_controller(vid, pid)) {
            extern void stadia_mount_cb(uint8_t dev_addr);
            stadia_mount_cb(actual_addr);
        }
    }
    if (ui_) {
        // Total joysticks = HID joysticks + Xbox controllers
        ui_->usb_connect_state(kb_count, mouse_count, joy_count + xinput_joy_count);
    }
}

void tuh_hid_unmounted_cb(uint8_t dev_addr) {
    HID_TYPE tp = tuh_hid_get_type(dev_addr);
    if (tp == HID_KEYBOARD) {
        // printf("A keyboard device (address %d) is unmounted\r\n", dev_addr);
        --kb_count;
    }
    else if (tp == HID_MOUSE) {
        // printf("A mouse device (address %d) is unmounted\r\n", dev_addr);
        --mouse_count;
    }
    else if (tp == HID_JOYSTICK) {
        // printf("A joystick device (address %d) is unmounted\r\n", dev_addr);
        --joy_count;
    }
    auto it = device.find(dev_addr);
    if (it != device.end()) {
        delete[] it->second;
        device.erase(it);
    }
    if (ui_) {
        ui_->usb_connect_state(kb_count, mouse_count, joy_count);
    }
}

// invoked ISR context
void tuh_hid_isr(uint8_t dev_addr, xfer_result_t event) {
    (void) dev_addr;
    (void) event;
}

}

HidInput::HidInput() {
    key_states.resize(128);
    std::fill(key_states.begin(), key_states.end(), 0);

    JOY_GPIO_INIT(JOY1_UP);
    JOY_GPIO_INIT(JOY1_DOWN);
    JOY_GPIO_INIT(JOY1_LEFT);
    JOY_GPIO_INIT(JOY1_RIGHT);
    JOY_GPIO_INIT(JOY1_FIRE);
    JOY_GPIO_INIT(JOY0_UP);
    JOY_GPIO_INIT(JOY0_DOWN);
    JOY_GPIO_INIT(JOY0_LEFT);
    JOY_GPIO_INIT(JOY0_RIGHT);
    JOY_GPIO_INIT(JOY0_FIRE);
}

HidInput& HidInput::instance() {
    static HidInput hid;
    return hid;
}

void HidInput::set_ui(UserInterface& ui) {
    ui_ = &ui;
}

void HidInput::open(const std::string& kbdev, const std::string& mousedev, const std::string joystickdev) {
}

void HidInput::force_usb_mouse() {
    ui_->set_mouse_enabled(true);
}

void HidInput::handle_keyboard() {
    for (auto it : device) {
        if (tuh_hid_get_type(it.first) != HID_KEYBOARD) {
            continue;
        }
        if (tuh_hid_is_mounted(it.first) && !tuh_hid_is_busy(it.first)) {
            hid_keyboard_report_t* kb = (hid_keyboard_report_t*)it.second;

            // Check for Ctrl+F12 to toggle mouse mode
            static bool last_toggle_state = false;
            bool ctrl_pressed = (kb->modifier & KEYBOARD_MODIFIER_LEFTCTRL) || (kb->modifier & KEYBOARD_MODIFIER_RIGHTCTRL);
            bool f12_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == TOGGLE_MOUSE_MODE) {
                    f12_pressed = true;
                    break;
                }
            }
            if (ctrl_pressed && f12_pressed) {
                // Toggle mouse mode on Ctrl+F12
                if (!last_toggle_state) {
                    ui_->set_mouse_enabled(!ui_->get_mouse_enabled());
                    last_toggle_state = true;
                }
            } else {
                last_toggle_state = false;
            }
            
            // Check for Alt + / to send INSERT
            bool alt_pressed = (kb->modifier & KEYBOARD_MODIFIER_LEFTALT) || (kb->modifier & KEYBOARD_MODIFIER_RIGHTALT);
            bool slash_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == HID_KEY_SLASH) {
                    slash_pressed = true;
                    break;
                }
            }
            
            // Check for Alt + Plus (=) to set 270MHz
            static bool last_plus_state = false;
            bool plus_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == HID_KEY_EQUAL) {
                    plus_pressed = true;
                    break;
                }
            }
            if (alt_pressed && plus_pressed) {
                if (!last_plus_state) {
                    set_sys_clock_khz(270000, false);
                    last_plus_state = true;
                }
            } else {
                last_plus_state = false;
            }
            
            // Check for Alt + Minus to set 150MHz
            static bool last_minus_state = false;
            bool minus_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == HID_KEY_MINUS) {
                    minus_pressed = true;
                    break;
                }
            }
            if (alt_pressed && minus_pressed) {
                if (!last_minus_state) {
                    set_sys_clock_khz(150000, false);
                    last_minus_state = true;
                }
            } else {
                last_minus_state = false;
            }
            
            // Check for Ctrl+F8 to restore joystick event reporting (send 0x14)
            static bool last_joy_restore_state = false;
            bool f8_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == RESTORE_JOYSTICK_KEY) {
                    f8_pressed = true;
                    break;
                }
            }
            
            if (ctrl_pressed && f8_pressed) {
                if (!last_joy_restore_state) {
                    // Show visual feedback on OLED
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 15, 15, 2, (char*)"JOYSTICK");
                    ssd1306_draw_string(&disp, 30, 35, 1, (char*)"MODE");
                    ssd1306_draw_string(&disp, 15, 50, 1, (char*)"Ctrl+F8");
                    ssd1306_show(&disp);
                    
                    // Send 0x14 (SET JOYSTICK EVENT REPORTING) to HD6301
                    hd6301_receive_byte(0x14);
                    
                    // Small delay so user can see the message
                    sleep_ms(500);
                    
                    last_joy_restore_state = true;
                }
            } else {
                last_joy_restore_state = false;
            }
            
            // Check for Ctrl+F11 to trigger XRESET (HD6301 hardware reset)
            static bool last_reset_state = false;
            bool f11_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == XRESET_KEY) {
                    f11_pressed = true;
                    break;
                }
            }
            
            if (ctrl_pressed && f11_pressed) {
                if (!last_reset_state) {
                    // Show visual feedback on OLED
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 30, 20, 2, (char*)"RESET");
                    ssd1306_draw_string(&disp, 20, 45, 1, (char*)"Ctrl+F11");
                    ssd1306_show(&disp);
                    
                    // Small delay so user can see the message
                    sleep_ms(500);
                    
                    // Trigger the reset
                    hd6301_trigger_reset();
                    last_reset_state = true;
                }
            } else {
                last_reset_state = false;
            }
            
            // Check for Ctrl+F9 to toggle Joystick 0 (D-SUB <-> USB)
            static bool last_joy0_state = false;
            bool f9_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == HID_KEY_F9) {
                    f9_pressed = true;
                    break;
                }
            }
            
            if (ctrl_pressed && f9_pressed) {
                if (!last_joy0_state) {
                    ui_->toggle_joystick_source(0);  // Toggle Joystick 0
                    last_joy0_state = true;
                }
            } else {
                last_joy0_state = false;
            }
            
            // Check for Ctrl+F10 to toggle Joystick 1 (D-SUB <-> USB)
            static bool last_joy1_state = false;
            bool f10_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == HID_KEY_F10) {
                    f10_pressed = true;
                    break;
                }
            }
            
            if (ctrl_pressed && f10_pressed) {
                if (!last_joy1_state) {
                    ui_->toggle_joystick_source(1);  // Toggle Joystick 1
                    last_joy1_state = true;
                }
            } else {
                last_joy1_state = false;
            }
            
            // Check for Alt+[ to send Atari keypad /
            static bool last_keypad_slash_state = false;
            bool left_bracket_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == HID_KEY_BRACKET_LEFT) {
                    left_bracket_pressed = true;
                    break;
                }
            }
            
            // Check for Alt+] to send Atari keypad *
            static bool last_keypad_star_state = false;
            bool right_bracket_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == HID_KEY_BRACKET_RIGHT) {
                    right_bracket_pressed = true;
                    break;
                }
            }
            
            // Check for Caps Lock key press to toggle Caps Lock state
            static bool last_capslock_state = false;
            static bool capslock_on = false;  // Caps Lock state (persists)
            static bool capslock_send_pulse = false;  // Send momentary pulse to ST
            bool capslock_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == HID_KEY_CAPS_LOCK) {
                    capslock_pressed = true;
                    break;
                }
            }
            
            if (capslock_pressed && !last_capslock_state) {
                // Caps Lock key was just pressed - toggle state
                capslock_on = !capslock_on;
                capslock_send_pulse = true;  // Send a pulse to the Atari ST
                last_capslock_state = true;
                
                // Update USB keyboard LED to match state
                // LED report: bit 1 = Caps Lock (0x02)
                uint8_t led_report = capslock_on ? 0x02 : 0x00;
                
                // Try multiple interface indices - wireless keyboards (Logitech Unifying, etc)
                // may use different interface indices than wired keyboards
                // Try idx 0 first (standard), then 1, 2 for wireless receivers
                bool led_sent = false;
                for (uint8_t idx = 0; idx < 3 && !led_sent; idx++) {
                    if (tuh_hid_set_report(it.first, idx, 0, HID_REPORT_TYPE_OUTPUT, &led_report, sizeof(led_report))) {
                        led_sent = true;
                    }
                }
            } else if (!capslock_pressed) {
                last_capslock_state = false;
                capslock_send_pulse = false;  // Clear pulse after key released
            }
            
            // Translate the USB HID codes into ST keys that are currently down
            char st_keys[6];
            for (int i = 0; i < 6; ++i) {
                if ((kb->keycode[i] > 0) && (kb->keycode[i] < 128)) {
                    // If Alt + / is pressed, replace / with INSERT
                    if (alt_pressed && kb->keycode[i] == HID_KEY_SLASH) {
                        st_keys[i] = ATARI_INSERT;
                    }
                    // If Alt + [ is pressed, send Atari keypad / (scancode 101)
                    else if (alt_pressed && kb->keycode[i] == HID_KEY_BRACKET_LEFT) {
                        st_keys[i] = 101;  // Atari keypad /
                    }
                    // If Alt + ] is pressed, send Atari keypad * (scancode 102)
                    else if (alt_pressed && kb->keycode[i] == HID_KEY_BRACKET_RIGHT) {
                        st_keys[i] = 102;  // Atari keypad *
                    }
                    // If Alt + Plus or Alt + Minus, don't send to Atari (used for clock control)
                    else if (alt_pressed && (kb->keycode[i] == HID_KEY_EQUAL || kb->keycode[i] == HID_KEY_MINUS)) {
                        st_keys[i] = 0;
                    }
                    // If Ctrl+F11, don't send to Atari (used for XRESET)
                    else if (ctrl_pressed && kb->keycode[i] == XRESET_KEY) {
                        st_keys[i] = 0;
                    }
                    // If Ctrl+F10, don't send to Atari (used for Joy1 toggle)
                    else if (ctrl_pressed && kb->keycode[i] == HID_KEY_F10) {
                        st_keys[i] = 0;
                    }
                    // If Ctrl+F9, don't send to Atari (used for Joy0 toggle)
                    else if (ctrl_pressed && kb->keycode[i] == HID_KEY_F9) {
                        st_keys[i] = 0;
                    }
                    else {
                        st_keys[i] = st_key_lookup_hid_gb[kb->keycode[i]];
                    }
                }
                else {
                    st_keys[i] = 0;
                }
            }
            
            // Handle Caps Lock as a toggle - send a momentary pulse to ST when toggled
            // Atari ST Caps Lock scancode is 58
            const int ATARI_CAPSLOCK = 58;
            
            // Go through all ST keys and update their state
            for (int i = 1; i < key_states.size(); ++i) {
                bool down = false;
                
                // Special handling for Caps Lock - send pulse only when toggling
                if (i == ATARI_CAPSLOCK) {
                    down = capslock_send_pulse;  // Send pulse when toggled, not persistent state
                } else {
                    // Normal key handling
                    for (int j = 0; j < 6; ++j) {
                        if (st_keys[j] == i) {
                            down = true;
                            break;
                        }
                    }
                }
                
                key_states[i] = down ? 1 : 0;
            }

            // Handle modifier keys
            key_states[ATARI_LSHIFT] = (kb->modifier & KEYBOARD_MODIFIER_LEFTSHIFT) ? 1 : 0;
            key_states[ATARI_RSHIFT] = (kb->modifier & KEYBOARD_MODIFIER_RIGHTSHIFT) ? 1 : 0;
            key_states[ATARI_CTRL] = ((kb->modifier & KEYBOARD_MODIFIER_LEFTCTRL) ||
                                      (kb->modifier & KEYBOARD_MODIFIER_RIGHTCTRL)) ? 1 : 0;
            key_states[ATARI_ALT] = ((kb->modifier & KEYBOARD_MODIFIER_LEFTALT) ||
                                      (kb->modifier & KEYBOARD_MODIFIER_RIGHTALT)) ? 1 : 0;
            // Trigger the next report
            hid_app_request_report(it.first, it.second);
        }
    }
}

void HidInput::handle_mouse(const int64_t cpu_cycles) {
    int32_t x = 0;
    int32_t y = 0;
    
    // Debug disabled for performance - enable for troubleshooting
    #if 0
    static uint32_t debug_count = 0;
    if ((debug_count++ % 500) == 0 && debug_count > 1) {
        extern ssd1306_t disp;
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 10, 0, 1, (char*)"DEVICE MAP");
        
        char line[20];
        snprintf(line, sizeof(line), "Devices: %d", (int)device.size());
        ssd1306_draw_string(&disp, 5, 15, 1, line);
        
        int y_pos = 30;
        for (auto it : device) {
            HID_TYPE tp = tuh_hid_get_type(it.first);
            const char* type = (tp == HID_MOUSE) ? "M" : 
                              (tp == HID_KEYBOARD) ? "K" : 
                              (tp == HID_JOYSTICK) ? "J" : "?";
            snprintf(line, sizeof(line), "Addr %d: %s", it.first, type);
            ssd1306_draw_string(&disp, 5, y_pos, 1, line);
            y_pos += 15;
            if (y_pos > 50) break;
        }
        
        ssd1306_show(&disp);
    }
    #endif
    
    for (auto it : device) {
        // Decode if this is a multi-interface mouse (key > 128)
        uint8_t actual_addr = (it.first >= 128) ? (it.first - 128) : it.first;
        
        if (tuh_hid_get_type(it.first) != HID_MOUSE) {
            continue;
        }
        
        // Debug disabled for performance
        #if 0
        static uint32_t mouse_handler_count = 0;
        if ((mouse_handler_count++ % 500) == 0 && mouse_handler_count > 1) {
            extern ssd1306_t disp;
            ssd1306_clear(&disp);
            ssd1306_draw_string(&disp, 15, 0, 1, (char*)"MOUSE HANDLER");
            
            char line1[20];
            snprintf(line1, sizeof(line1), "Key:%d Addr:%d", it.first, actual_addr);
            ssd1306_draw_string(&disp, 5, 15, 1, line1);
            
            char line2[20];
            bool mounted = tuh_hid_is_mounted(it.first);
            bool busy = tuh_hid_is_busy(it.first);
            snprintf(line2, sizeof(line2), "M:%d B:%d", mounted, busy);
            ssd1306_draw_string(&disp, 5, 30, 1, line2);
            
            HID_ReportInfo_t* info = tuh_hid_get_report_info(it.first);
            char line3[20];
            snprintf(line3, sizeof(line3), "Info: %s", info ? "YES" : "NULL");
            ssd1306_draw_string(&disp, 5, 45, 1, line3);
            
            ssd1306_show(&disp);
            sleep_ms(1500);
        }
        #endif
        
        if (tuh_hid_is_mounted(it.first) && !tuh_hid_is_busy(it.first)) {  // Use key, not actual_addr
            hid_mouse_report_t* mouse = (hid_mouse_report_t*)it.second;

            const uint8_t* js = it.second;
            HID_ReportInfo_t* info = tuh_hid_get_report_info(it.first);  // Use key
            
            // Check if this is a multi-interface mouse (Logitech Unifying)
            // These have key >= 128 AND are boot protocol mice (have simple 4-byte reports)
            bool is_multi_interface_mouse = (it.first >= 128);
            
            if (is_multi_interface_mouse) {
                // For multi-interface mice (Logitech Unifying), HID parser fails
                // Use direct boot protocol format parsing instead
                // Standard boot protocol mouse format:
                // Byte 0: Buttons
                // Byte 1: X movement (signed)
                // Byte 2: Y movement (signed)
                // Byte 3: Wheel (optional)
                
                int8_t buttons = js[0];
                int8_t dx = (int8_t)js[1];
                int8_t dy = (int8_t)js[2];
                
                // Filter out weird idle value (0xFF in byte 2)
                if (js[2] == 0xFF && js[1] == 0x00) {
                    dy = 0;  // Idle state, not actual movement
                }
                
                x = dx;
                y = dy;
                
                // Update button state
                mouse_state = (mouse_state & 0xfd) | ((buttons & 0x01) ? 2 : 0);  // Left button
                mouse_state = (mouse_state & 0xfe) | ((buttons & 0x02) ? 1 : 0);  // Right button
            }
            else if (info) {
                // Standard HID parser for regular mice
                int8_t buttons = 0;

                for (uint8_t i = 0; i < info->TotalReportItems; ++i) {
                    HID_ReportItem_t* item = &info->ReportItems[i];
                    // Update the report item value if it is contained within the current report
                    if (!(USB_GetHIDReportItemInfo((const uint8_t*)js, item)))
                        continue;
                    // Determine what report item is being tested, process updated value as needed
                    if ((item->Attributes.Usage.Page == USAGE_PAGE_BUTTON) && (item->ItemType == HID_REPORT_ITEM_In)) {
                        buttons |= (item->Value ? 1 : 0) << (item->Attributes.Usage.Usage - 1);
                    }
                    else if ((item->Attributes.Usage.Page == USAGE_PAGE_GENERIC_DCTRL) &&
                                ((item->Attributes.Usage.Usage == USAGE_X) ||
                                 (item->Attributes.Usage.Usage == USAGE_Y)) &&
                                 (item->ItemType == HID_REPORT_ITEM_In)) {
                        if (item->Attributes.Usage.Usage == USAGE_X) {
                            x = GET_I32_VALUE(item);
                        }
                        else {
                            y = GET_I32_VALUE(item);
                        }
                    }
                }
                // Update button state
                mouse_state = (mouse_state & 0xfd) | ((buttons & MOUSE_BUTTON_LEFT) ? 2 : 0);
                mouse_state = (mouse_state & 0xfe) | ((buttons & MOUSE_BUTTON_RIGHT) ? 1 : 0);
            }
            // Trigger the next report (use device key, not actual_addr, for multi-interface devices)
            hid_app_request_report(it.first, it.second);
        }
    }
    // Handle the mouse acceleration/deceleration configured in the UI.
    double accel = 1.0 + ((double)ui_->get_mouse_speed() * 0.1);
    AtariSTMouse::instance().set_speed((int)((double)x * accel), (int)((double)y * accel));
}

bool HidInput::get_usb_joystick(int addr, uint8_t& axis, uint8_t& button) {
	const int DEAD_ZONE = 0x10; // Dead zone value, can be adjusted
    // Stadia fallback: If this is a Stadia controller, parse known 11-byte simple format
    {
        uint16_t vid = 0, pid = 0;
        tuh_vid_pid_get(addr & 0x7F, &vid, &pid);
        extern bool stadia_is_controller(uint16_t, uint16_t);
        if (stadia_is_controller(vid, pid)) {
            if (tuh_hid_is_mounted(addr) && !tuh_hid_is_busy(addr)) {
                const uint8_t* js = device[addr];
                // Expected: 11 bytes: [0-1 header/buttons?][2-3 buttons?][4-5 LX/LY][6-7 RX/RY][8 LT][9 RT][10 pad]
                if (js) {
                    // Reset outputs
                    axis = 0;
                    button = 0;
                    
                    // Stadia report format (from stadia-vigem project):
                    // Byte 0: 0x03 (header)
                    // Byte 1: D-Pad hat switch (0-7)
                    // Byte 2: System buttons (bit 7=RS, 6=Options, 5=Menu, 4=Stadia)
                    // Byte 3: Face buttons (bit 6=A, 5=B, 4=X, 3=Y, 2=LB, 1=RB, 0=LS)
                    // Bytes 4-5: Left stick X, Y
                    // Bytes 6-7: Right stick X, Y
                    // Bytes 8-9: Left/Right triggers
                    
                    uint8_t dpad = js[1];  // D-Pad in byte 1!
                    uint8_t lx = js[4];
                    uint8_t ly = js[5];
                    uint8_t lt = js[8];
                    uint8_t rt = js[9];
                    
                    // D-Pad has priority (hat switch 0-7, 8/15 = center)
                    bool dpad_active = false;
                    if (dpad < 8) {
                        dpad_active = true;
                        switch(dpad) {
                            case 0: axis |= 0x01; break; // Up
                            case 1: axis |= 0x01 | 0x08; break; // Up-Right
                            case 2: axis |= 0x08; break; // Right
                            case 3: axis |= 0x02 | 0x08; break; // Down-Right
                            case 4: axis |= 0x02; break; // Down
                            case 5: axis |= 0x02 | 0x04; break; // Down-Left
                            case 6: axis |= 0x04; break; // Left
                            case 7: axis |= 0x01 | 0x04; break; // Up-Left
                        }
                    }
                    
                    // If D-Pad not active, use analog stick
                    if (!dpad_active) {
                        if (lx < 0x80 - DEAD_ZONE) axis |= 0x04; // Left
                        else if (lx > 0x80 + DEAD_ZONE) axis |= 0x08; // Right
                        if (ly < 0x80 - DEAD_ZONE) axis |= 0x01; // Up
                        else if (ly > 0x80 + DEAD_ZONE) axis |= 0x02; // Down
                    }
                    
                    // Fire buttons (byte 3): A, B, X, Y, LB, RB, LS
                    // Bit 6=A, 5=B, 4=X, 3=Y, 2=LB, 1=RB, 0=LS
                    if (js[3] != 0) button = 1;
                    
                    // Also check triggers for fire
                    if (rt > 0x10 || lt > 0x10) button = 1;
                    
                    // Debug: Show D-Pad byte and output
                    #if 0
                    static uint32_t stadia_debug_count = 0;
                    if ((stadia_debug_count++ % 50) == 0) {
                        extern ssd1306_t disp;
                        ssd1306_clear(&disp);
                        char line[20];
                        snprintf(line, sizeof(line), "DPad:%02X B3:%02X", dpad, js[3]);
                        ssd1306_draw_string(&disp, 0, 0, 1, line);
                        snprintf(line, sizeof(line), "LX%02X LY%02X", lx, ly);
                        ssd1306_draw_string(&disp, 0, 13, 1, line);
                        snprintf(line, sizeof(line), "LT%02X RT%02X", lt, rt);
                        ssd1306_draw_string(&disp, 0, 26, 1, line);
                        snprintf(line, sizeof(line), "act:%d", dpad_active);
                        ssd1306_draw_string(&disp, 0, 39, 1, line);
                        snprintf(line, sizeof(line), "Ax%02X Bt%d", axis, button);
                        ssd1306_draw_string(&disp, 0, 52, 1, line);
                        ssd1306_show(&disp);
                    }
                    #endif
                    
                    // Queue next report
                    hid_app_request_report(addr, device[addr]);
                    return true;
                }
            }
        }
    }

    if (tuh_hid_is_mounted(addr) && !tuh_hid_is_busy(addr)) {
        const uint8_t* js = device[addr];
        HID_ReportInfo_t* info = tuh_hid_get_report_info(addr);
        if (info) {
            for (uint8_t i = 0; i < info->TotalReportItems; ++i) {
                HID_ReportItem_t* item = &info->ReportItems[i];
                // Update the report item value if it is contained within the current report
                if (!(USB_GetHIDReportItemInfo((const uint8_t*)js, item)))
                    continue;
                // Determine what report item is being tested, process updated value as needed
                if ((item->Attributes.Usage.Page == USAGE_PAGE_BUTTON) && (item->ItemType == HID_REPORT_ITEM_In)) {
                    // Set button state directly (don't accumulate with |=)
                    // This ensures button releases are properly detected
                    if (item->Value) {
                        button = 1;
                    }
                }
                else if ((item->Attributes.Usage.Page   == USAGE_PAGE_GENERIC_DCTRL) &&
                            ((item->Attributes.Usage.Usage == USAGE_X) || (item->Attributes.Usage.Usage == USAGE_Y)) &&
                            (item->ItemType == HID_REPORT_ITEM_In)) {
                    int bit;
                    if (item->Attributes.Usage.Usage == USAGE_X) {
                        bit = 2;
                    }
                    else {
                        bit = 0;
                    }
                    // Up and left have a value < 0x80 (0 for digital)
                    // Down and right have a value > 0x80 (0xff for digital)
					if (item->Value > (0x80 - DEAD_ZONE) && item->Value < (0x80 + DEAD_ZONE)) {
                    
                    axis &= ~(0x3 << bit);
                    }
                    else{
                        axis &= ~(0x3 << bit);
                        if (item->Value < 0x80 - DEAD_ZONE) {
                            axis |= 1 << bit;
                        }
                        else if (item->Value > 0x80 + DEAD_ZONE) {
                            axis |= 1 << (bit + 1);
                        }
                    }
                }
            }
        }
        // Trigger the next report
        hid_app_request_report(addr, device[addr]);
        return true;
    }
    return false;
}

bool HidInput::get_ps4_joystick(int joystick_num, uint8_t& axis, uint8_t& button) {
    // Get PS4 controller state
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        ps4_controller_t* ps4 = ps4_get_controller(dev_addr);
        if (ps4 && ps4->connected) {
            // Found a connected PS4 controller!
            ps4_to_atari(ps4, joystick_num, &axis, &button);
            
            // Debug disabled for performance
            // (Enable only for troubleshooting)
            #if 0
            static uint32_t debug_count = 0;
            if ((debug_count++ % 500) == 0) {
                printf("PS4->Atari: Joy%d axis=0x%02X fire=%d\n", joystick_num, axis, button);
            }
            #endif
            
            return true;
        }
    }
    
    return false;
}

// Forward declaration for Xbox-to-Atari mapper
extern "C" bool xinput_to_atari_joystick(int joystick_num, uint8_t* axis, uint8_t* button);

bool HidInput::get_xbox_joystick(int joystick_num, uint8_t& axis, uint8_t& button) {
    // Use official tusb_xinput driver via Atari mapper
    return xinput_to_atari_joystick(joystick_num, &axis, &button);
}

bool HidInput::get_switch_joystick(int joystick_num, uint8_t& axis, uint8_t& button) {
    // Get Switch controller state
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        switch_controller_t* sw = switch_get_controller(dev_addr);
        if (sw && sw->connected) {
            // Found a connected Switch controller!
            switch_to_atari(sw, joystick_num, &axis, &button);
            return true;
        }
    }
    return false;
}

bool HidInput::get_stadia_joystick(int joystick_num, uint8_t& axis, uint8_t& button) {
    // Get Stadia controller state
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        stadia_controller_t* stadia = stadia_get_controller(dev_addr);
        if (stadia && stadia->connected) {
            // Found a connected Stadia controller!
            stadia_to_atari(stadia, joystick_num, &axis, &button);
            return true;
        }
    }
    return false;
}

void HidInput::handle_joystick() {
    // Find the joystick addresses
    std::vector<int> joystick_addr;
    int next_joystick = 0;
    for (auto it : device) {
        if (tuh_hid_get_type(it.first) == HID_JOYSTICK) {
            joystick_addr.push_back(it.first);
        }
    }

    // See if the joysticks are GPIO or USB
    for (int joystick = 1; joystick >= 0; --joystick) {
        // Initialize axis and button for each joystick separately to prevent state bleed
        uint8_t axis = 0;
        uint8_t button = 0;
        
        uint8_t joy_setting = ui_->get_joystick();
        if (joy_setting & (1 << joystick)) {
            // GPIO path
            g_gpio_path_count++;
            if (joystick == 1) {
                mouse_state = (mouse_state & 0xfe) | (gpio_get(JOY1_FIRE) ? 0 : 1);
                axis |= (gpio_get(JOY1_UP)) ? 0 : 1;
                axis |= (gpio_get(JOY1_DOWN)) ? 0 : 2;
                axis |= (gpio_get(JOY1_LEFT)) ? 0 : 4;
                axis |= (gpio_get(JOY1_RIGHT)) ? 0 : 8;
                joystick_state &= ~(0xf << 4);
                joystick_state |= (axis << 4);
            }
            else if (!ui_->get_mouse_enabled()) {
                mouse_state = (mouse_state & 0xfd) | (gpio_get(JOY0_FIRE) ? 0 : 2);
                axis |= (gpio_get(JOY0_UP)) ? 0 : 1;
                axis |= (gpio_get(JOY0_DOWN)) ? 0 : 2;
                axis |= (gpio_get(JOY0_LEFT)) ? 0 : 4;
                axis |= (gpio_get(JOY0_RIGHT)) ? 0 : 8;
                joystick_state &= ~0xf;
                joystick_state |= axis;
            }
        }
        else {
            // USB path
            g_usb_path_count++;
            
            // Try all sources: USB HID joystick, then PS4, then Xbox
            bool got_input = false;
            
            // First priority: USB HID joystick
            if (next_joystick < joystick_addr.size()) {
                if (get_usb_joystick(joystick_addr[next_joystick], axis, button)) {
                    got_input = true;
                    g_hid_joy_success++;  // Track HID success
                }
                ++next_joystick;
            }
            
            // Second priority: PS4 controller (always check, even if HID exists)
            // FIX: PS4 and Xbox were being skipped if ANY HID entry existed, even disconnected ones
            if (!got_input && get_ps4_joystick(joystick, axis, button)) {
                got_input = true;
                g_ps4_success++;  // Track PS4 success
            }
            
            // Third priority: Switch controller
            if (!got_input && get_switch_joystick(joystick, axis, button)) {
                got_input = true;
                g_switch_success++;  // Track Switch success
            }
            
            // Fourth priority: Stadia controller - NOW USES GENERIC HID PATH
            // (Stadia is detected as standard HID joystick, handled above)
            
            // Fifth priority: Xbox controller (always check as final fallback)
            // FIX: This ensures Xbox is checked even if stale HID entries exist
            if (!got_input && get_xbox_joystick(joystick, axis, button)) {
                got_input = true;
                g_xbox_success++;  // Track Xbox success
            }
            
            // Update joystick state if we got input from either source
            if (got_input) {
                if (joystick == 0) {
                    if (!ui_->get_mouse_enabled()) {
                        mouse_state = (mouse_state & 0xfd) | (button ? 2 : 0);
                        joystick_state &= ~0xf;
                        joystick_state |= axis;
                    }
                }
                else {
                    mouse_state = (mouse_state & 0xfe) | (button ? 1 : 0);
                    joystick_state &= ~(0xf << 4);
                    joystick_state |= (axis << 4);
                }
            }
        }
    }
}

void HidInput::reset() {
     std::fill(key_states.begin(), key_states.end(), 0);   
}

unsigned char HidInput::keydown(const unsigned char code) const {
    if (code < 128) {
        return key_states[code];
    }
    return 0;
}

int HidInput::mouse_buttons() const {
    return mouse_state;
}

unsigned char HidInput::joystick() const {
    return joystick_state;
}

bool HidInput::mouse_enabled() const {
    return ui_->get_mouse_enabled();
}

unsigned char st_keydown(const unsigned char code){
    return HidInput::instance().keydown(code);
}

int st_mouse_buttons() {
    return HidInput::instance().mouse_buttons();
}

unsigned char st_joystick() {
    return HidInput::instance().joystick();
}

int st_mouse_enabled() {
    return HidInput::instance().mouse_enabled() ? 1 : 0;
}

void update_joystick_state() {
    // Called from 6301 emulator when DR2/DR4 registers are read
    // Provides on-demand joystick updates for better timing accuracy
    HidInput::instance().handle_joystick();
}
