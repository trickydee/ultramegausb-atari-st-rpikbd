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
#include "xinput.h"
#include "ps4_controller.h"
#include <map>

extern ssd1306_t disp;  // External reference to display

// Mouse toggle key is set to Ctrl+F12
#define TOGGLE_MOUSE_MODE 0x45  // F12 key (69 decimal)

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
static int joy_count = 0;

extern "C" {

void tuh_hid_mounted_cb(uint8_t dev_addr) {
    HID_TYPE tp = tuh_hid_get_type(dev_addr);
    if (tp == HID_KEYBOARD) {
        // printf("A keyboard device (address %d) is mounted\r\n", dev_addr);
        device[dev_addr] = new uint8_t[sizeof(hid_keyboard_report_t)];
        hid_app_request_report(dev_addr, device[dev_addr]);
        ++kb_count;
    }
    else if (tp == HID_MOUSE) {
        // printf("A mouse device (address %d) is mounted\r\n", dev_addr);
        device[dev_addr] = new uint8_t[tuh_hid_get_report_size(dev_addr)];
        hid_app_request_report(dev_addr, device[dev_addr]);
        ++mouse_count;
    }
    else if (tp == HID_JOYSTICK) {
        // printf("A joystick device (address %d) is mounted\r\n", dev_addr);
        device[dev_addr] = new uint8_t[tuh_hid_get_report_size(dev_addr)];
        hid_app_request_report(dev_addr, device[dev_addr]);
        ++joy_count;
    }
    if (ui_) {
        ui_->usb_connect_state(kb_count, mouse_count, joy_count);
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
            
            // Translate the USB HID codes into ST keys that are currently down
            char st_keys[6];
            for (int i = 0; i < 6; ++i) {
                if ((kb->keycode[i] > 0) && (kb->keycode[i] < 128)) {
                    // If Alt + / is pressed, replace / with INSERT
                    if (alt_pressed && kb->keycode[i] == HID_KEY_SLASH) {
                        st_keys[i] = ATARI_INSERT;
                    }
                    // If Alt + Plus or Alt + Minus, don't send to Atari (used for clock control)
                    else if (alt_pressed && (kb->keycode[i] == HID_KEY_EQUAL || kb->keycode[i] == HID_KEY_MINUS)) {
                        st_keys[i] = 0;
                    }
                    // If Ctrl+F11, don't send to Atari (used for XRESET)
                    else if (ctrl_pressed && kb->keycode[i] == XRESET_KEY) {
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
            // Go through all ST keys and update their state
            for (int i = 1; i < key_states.size(); ++i) {
                bool down = false;
                for (int j = 0; j < 6; ++j) {
                    if (st_keys[j] == i) {
                        down = true;
                        break;
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
    for (auto it : device) {
        if (tuh_hid_get_type(it.first) != HID_MOUSE) {
            continue;
        }
        if (tuh_hid_is_mounted(it.first) && !tuh_hid_is_busy(it.first)) {
            hid_mouse_report_t* mouse = (hid_mouse_report_t*)it.second;

            const uint8_t* js = it.second;
            HID_ReportInfo_t* info = tuh_hid_get_report_info(it.first);
            if (info) {
                // Get the data from the HID report
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
            // Trigger the next report
            hid_app_request_report(it.first, it.second);
        }
    }
    // Handle the mouse acceleration/deceleration configured in the UI.
    double accel = 1.0 + ((double)ui_->get_mouse_speed() * 0.1);
    AtariSTMouse::instance().set_speed((int)((double)x * accel), (int)((double)y * accel));
}

bool HidInput::get_usb_joystick(int addr, uint8_t& axis, uint8_t& button) {
	const int DEAD_ZONE = 0x10; // Dead zone value, can be adjusted
    
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

bool HidInput::get_xbox_joystick(int joystick_num, uint8_t& axis, uint8_t& button) {
    // Get Xbox controller state from XInput module
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        xbox_controller_t* xbox = xinput_get_controller(dev_addr);
        if (xbox && xbox->connected && xbox->initialized) {
            // Found a connected Xbox controller!
            xinput_to_atari(xbox, joystick_num, &axis, &button);
            
            // Debug disabled for performance
            #if 0
            static uint32_t debug_count = 0;
            if ((debug_count++ % 500) == 0) {
                printf("Xbox->Atari: Joy%d axis=0x%02X fire=%d\n", joystick_num, axis, button);
            }
            #endif
            
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
        
        if (ui_->get_joystick() & (1 << joystick)) {
            // GPIO
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
            // Try USB joystick first, then Xbox controller
            bool got_input = false;
            
            // See if there is a USB HID joystick
            if (next_joystick < joystick_addr.size()) {
                if (get_usb_joystick(joystick_addr[next_joystick], axis, button)) {
                    got_input = true;
                }
                ++next_joystick;
            }
            
            // If no HID joystick, try PS4 controller, then Xbox
            if (!got_input) {
                if (get_ps4_joystick(joystick, axis, button)) {
                    got_input = true;
                } else if (get_xbox_joystick(joystick, axis, button)) {
                    got_input = true;
                }
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
