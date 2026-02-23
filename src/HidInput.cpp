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
#include "ps5_controller.h"
#include "psc_controller.h"
#include "ps3_controller.h"
#include "horipad_controller.h"
#include "gamecube_adapter.h"
#include "switch_controller.h"
#include "stadia_controller.h"
#include "xinput.h"
#include "runtime_toggle.h"  // For usb_runtime_is_enabled() and bt_runtime_is_enabled()
#include <map>
#include <set>
#include <deque>
#include <algorithm>
#include <bitset>
#include <cstring>

#if ENABLE_OLED_DISPLAY
extern ssd1306_t disp;  // External reference to display
#endif

// Mouse toggle key is set to Ctrl+F12
#define TOGGLE_MOUSE_MODE 0x45  // F12 key (69 decimal)

// Ctrl+F5 sends 0x08 (SET RELATIVE MOUSE MODE)
#define MOUSE_RELATIVE_KEY 0x3E  // F5 key (62 decimal)

// Ctrl+F6 sends 0x09 (SET ABSOLUTE MOUSE MODE)
#define MOUSE_ABSOLUTE_KEY 0x3F  // F6 key (63 decimal)

// Ctrl+F7 sends 0x0A (SET MOUSE KEYCODE MODE)
#define MOUSE_KEYCODE_KEY 0x40  // F7 key (64 decimal)

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
#define ATARI_CURSOR_UP   72
#define ATARI_CURSOR_DOWN 80
#define ATARI_KEY_P       25  // Atari ST scancode for 'P'
#define ATARI_KEY_O       24  // Atari ST scancode for 'O'
#define MAX_WHEEL_PULSES  32

#define GET_I32_VALUE(item)     (int32_t)(item->Value | ((item->Value & (1 << (item->Attributes.BitSize-1))) ? ~((1 << item->Attributes.BitSize) - 1) : 0))
#define JOY_GPIO_INIT(io)       gpio_init(io); gpio_set_dir(io, GPIO_IN); gpio_pull_up(io);

static std::map<int, uint8_t*> device;
static UserInterface* ui_ = nullptr;
static int kb_count = 0;
static int mouse_count = 0;
static int joy_count = 0;  // HID joysticks (not Xbox)
static std::set<uint8_t> gc_counted_devices;  // Track GameCube devices already counted

// Path counters (file-level static, accessed via getters)
static uint32_t g_gpio_path_count = 0;
static uint32_t g_usb_path_count = 0;
static uint32_t g_hid_joy_success = 0;
static uint32_t g_ps4_success = 0;
static uint32_t g_xbox_success = 0;
static uint32_t g_switch_success = 0;

// Llamatron (dual-stick) mode state
static bool g_llamatron_mode = false;
static bool g_llamatron_active = false;
static uint8_t g_llama_axis_joy1 = 0;
static uint8_t g_llama_fire_joy1 = 0;
static uint8_t g_llama_axis_joy0 = 0;
static uint8_t g_llama_fire_joy0 = 0;
static bool g_llamatron_restore_mouse = false;
// Llamatron pause/unpause button state
static bool g_llama_pause_button_prev = false;
static bool g_llama_paused = false;  // Track pause state to toggle between P and O

static std::deque<uint8_t> wheel_pulses;
static std::bitset<128> wheel_prev_mask;

static void enqueue_wheel_pulses(int delta) {
    if (delta == 0) {
        return;
    }

    // Positive delta = scroll down = cursor DOWN key
    // Negative delta = scroll up = cursor UP key
    const uint8_t key = (delta > 0) ? ATARI_CURSOR_DOWN : ATARI_CURSOR_UP;
    int steps = delta > 0 ? delta : -delta;
    steps = std::min(steps, 8);  // Avoid bursts from high-resolution wheels


    for (int i = 0; i < steps; ++i) {
        if (wheel_pulses.size() >= MAX_WHEEL_PULSES) {
            wheel_pulses.pop_front();
        }
        wheel_pulses.push_back(key);
    }
}


extern "C" {

// Forward declaration for Xbox pause button check
bool xinput_check_menu_or_start_button(void);

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

// GameCube adapter mount/unmount notifications (same pattern as Xbox)
void gc_notify_mount(uint8_t dev_addr) {
    // Check if already counted (prevent multi-interface duplicate counting)
    if (gc_counted_devices.find(dev_addr) == gc_counted_devices.end()) {
        gc_counted_devices.insert(dev_addr);
        joy_count++;
        xinput_notify_ui_mount();
    }
}

void gc_notify_unmount(uint8_t dev_addr) {
    // Remove from counted set and decrement counter
    if (gc_counted_devices.find(dev_addr) != gc_counted_devices.end()) {
        gc_counted_devices.erase(dev_addr);
        joy_count--;
        xinput_notify_ui_unmount();
    }
}

#if ENABLE_BLUEPAD32
// Functions for Bluetooth gamepad to notify UI of mount/unmount
extern "C" {
    // Forward declaration - implemented in bluepad32_platform.c
    int bluepad32_get_connected_count(void);
    bool bluepad32_get_gamepad(int idx, void* out_gamepad);
    bool bluepad32_to_atari_joystick(const void* gp_ptr, uint8_t* axis, uint8_t* button);
    bool bluepad32_get_keyboard(int idx, void* out_keyboard);
    bool bluepad32_peek_keyboard(int idx, void* out_keyboard);
    bool bluepad32_get_mouse(int idx, void* out_mouse);
    int bluepad32_get_keyboard_count(void);
    int bluepad32_get_mouse_count(void);
}

// Track Bluetooth gamepad count separately to avoid double-counting
static int bt_joy_count = 0;

// Flag to indicate UI needs updating (set by BT callbacks, cleared by main loop)
static volatile bool g_bt_ui_update_needed = false;

void bluepad32_notify_ui_update() {
    // Defer UI update to main loop to avoid blocking Bluetooth callbacks
    // This prevents Core 1 from being starved during pairing/connection
    g_bt_ui_update_needed = true;
}

// Called from bluepad32_platform.c when a Bluetooth gamepad connects
extern "C" void bluepad32_notify_mount() {
    bt_joy_count++;
    // Bluetooth joysticks are assigned to joystick 1 first (matching USB behavior)
    // Defer UI update to avoid blocking in Bluetooth callback
    bluepad32_notify_ui_update();
}

// Called from bluepad32_platform.c when a Bluetooth gamepad disconnects
extern "C" void bluepad32_notify_unmount() {
    if (bt_joy_count > 0) {
        bt_joy_count--;
        // Defer UI update to avoid blocking in Bluetooth callback
        bluepad32_notify_ui_update();
    }
}

// Called from bluepad32_platform.c when a Bluetooth keyboard connects
extern "C" void bluepad32_notify_keyboard_mount() {
    kb_count++;
    if (ui_) {
        ui_->usb_connect_state(kb_count, mouse_count, joy_count + xinput_joy_count + bt_joy_count);
    }
}

// Called from bluepad32_platform.c when a Bluetooth keyboard disconnects
extern "C" void bluepad32_notify_keyboard_unmount() {
    if (kb_count > 0) {
        kb_count--;
        if (ui_) {
            ui_->usb_connect_state(kb_count, mouse_count, joy_count + xinput_joy_count + bt_joy_count);
        }
    }
}

// Called from bluepad32_platform.c when a Bluetooth mouse connects
extern "C" void bluepad32_notify_mouse_mount() {
    mouse_count++;
    if (ui_) {
        ui_->usb_connect_state(kb_count, mouse_count, joy_count + xinput_joy_count + bt_joy_count);
    }
}

// Called from bluepad32_platform.c when a Bluetooth mouse disconnects
extern "C" void bluepad32_notify_mouse_unmount() {
    if (mouse_count > 0) {
        mouse_count--;
        if (ui_) {
            ui_->usb_connect_state(kb_count, mouse_count, joy_count + xinput_joy_count + bt_joy_count);
        }
    }
}

// Check if UI update is needed and perform it (called from main loop)
void bluepad32_check_ui_update() {
    if (g_bt_ui_update_needed) {
        g_bt_ui_update_needed = false;
        if (ui_) {
            ui_->usb_connect_state(kb_count, mouse_count, joy_count + xinput_joy_count + bt_joy_count);
        }
    }
}
#endif

static uint8_t count_connected_usb_gamepads() {
    uint8_t total = 0;
    total += ps4_connected_count();
    total += ps5_connected_count();
    total += psc_connected_count();
    total += ps3_connected_count();
    total += horipad_connected_count();
    total += switch_connected_count();
    total += stadia_connected_count();
    total += xinput_connected_count();
    total += gc_connected_count();
    return total;
}

static uint8_t count_connected_gamepads() {
    uint8_t total = count_connected_usb_gamepads();
#if ENABLE_BLUEPAD32
    if (bt_runtime_is_enabled()) {
        total += bluepad32_get_connected_count();
    }
#endif
    return total;
}

static bool collect_llamatron_sample(uint8_t& joy1_axis, uint8_t& joy1_fire,
                                     uint8_t& joy0_axis, uint8_t& joy0_fire) {
    if (ps4_llamatron_axes(&joy1_axis, &joy1_fire, &joy0_axis, &joy0_fire)) return true;
    if (ps5_llamatron_axes(&joy1_axis, &joy1_fire, &joy0_axis, &joy0_fire)) return true;
    if (horipad_llamatron_axes(&joy1_axis, &joy1_fire, &joy0_axis, &joy0_fire)) return true;
    if (ps3_llamatron_axes(&joy1_axis, &joy1_fire, &joy0_axis, &joy0_fire)) return true;
    if (switch_llamatron_axes(&joy1_axis, &joy1_fire, &joy0_axis, &joy0_fire)) return true;
    if (stadia_llamatron_axes(&joy1_axis, &joy1_fire, &joy0_axis, &joy0_fire)) return true;
    if (xinput_llamatron_axes(&joy1_axis, &joy1_fire, &joy0_axis, &joy0_fire)) return true;
    if (gc_llamatron_axes(&joy1_axis, &joy1_fire, &joy0_axis, &joy0_fire)) return true;
#if ENABLE_BLUEPAD32
    extern bool bluepad32_llamatron_axes(uint8_t* joy1_axis, uint8_t* joy1_fire,
                                         uint8_t* joy0_axis, uint8_t* joy0_fire);
    if (bluepad32_llamatron_axes(&joy1_axis, &joy1_fire, &joy0_axis, &joy0_fire)) return true;
#endif
    return false;
}

// Check for menu/options/start button press across all controller types
// Returns true if button is currently pressed
static bool check_llamatron_pause_button() {
    // Check PS4 controllers (Options button)
    for (uint8_t i = 0; i < 255; i++) {
        ps4_controller_t* ps4 = ps4_get_controller(i);
        if (ps4 && ps4->connected && ps4->report.options) {
            return true;
        }
    }
    
    // Check PS3 controllers (Start button)
    for (uint8_t i = 0; i < 255; i++) {
        ps3_controller_t* ps3 = ps3_get_controller(i);
        if (ps3 && ps3->connected && (ps3->report.buttons[0] & (1 << PS3_BTN_START))) {
            return true;
        }
    }
    
    // Check Xbox controllers (Menu button = BACK, Start button = START)
    if (xinput_check_menu_or_start_button()) {
        return true;
    }
    
    // Check Switch controllers (Plus button = menu)
    for (uint8_t i = 0; i < 255; i++) {
        switch_controller_t* sw = switch_get_controller(i);
        if (sw && sw->connected && (sw->buttons & SWITCH_BTN_PLUS)) {
            return true;
        }
    }
    
    // Check Stadia controllers (Menu button)
    for (uint8_t i = 0; i < 255; i++) {
        stadia_controller_t* stadia = stadia_get_controller(i);
        if (stadia && stadia->connected && (stadia->buttons & STADIA_BTN_START)) {
            return true;
        }
    }
    
    // Check GameCube controllers (Start button)
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        gc_adapter_t* gc = gc_get_adapter(dev_addr);
        if (gc && gc->connected && gc->active_port != 0xFF) {
            // Check if START button is pressed (buttons2 bit 0)
            if (gc->report.port[gc->active_port].buttons2 & GC_BTN_START) {
                return true;
            }
        }
    }
    
    return false;
}

#if ENABLE_OLED_DISPLAY
static void draw_centered_text(const char* text, int y, int scale) {
    if (!text || !*text) {
        return;
    }
    size_t len = strlen(text);
    if (len > 16) {
        len = 16;
    }
    char buf[17];
    memcpy(buf, text, len);
    buf[len] = '\0';
    int char_width = 6 * scale;
    int width = static_cast<int>(len) * char_width;
    int x = (SSD1306_WIDTH - width) / 2;
    if (x < 0) {
        x = 0;
    }
    ssd1306_draw_string(&disp, x, y, scale, buf);
}
#endif

static void show_llamatron_status(const char* line1, const char* line2) {
    if (line1 && *line1) {
        printf("LLAMATRON: %s", line1);
        if (line2 && *line2) {
            printf(" - %s", line2);
        }
        printf("\n");
    }
#if ENABLE_OLED_DISPLAY
    ssd1306_clear(&disp);
    // Use smaller scale for the title lines to ensure they fit
    draw_centered_text("LLAMATRON", 6, 1);
    draw_centered_text("MODE", 24, 1);
    if (line1 && *line1) {
        // Emphasize the status line
        draw_centered_text(line1, 44, 2);
    }
    if (line2 && *line2) {
        draw_centered_text(line2, 58, 1);
    }
    ssd1306_show(&disp);
    sleep_ms(1000);
#endif
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
        // Check if this is a GameCube adapter - if so, skip counting here
        // (it's already counted via gc_notify_mount())
        uint16_t vid, pid;
        tuh_vid_pid_get(actual_addr, &vid, &pid);
        extern bool gc_is_adapter(uint16_t vid, uint16_t pid);
        bool is_gamecube = gc_is_adapter(vid, pid);
        
        // Check if already registered (prevent multi-interface duplicate counting)
        // Also skip if it's a GameCube adapter (counted separately)
        if (!is_gamecube && device.find(actual_addr) == device.end()) {
            device[actual_addr] = new uint8_t[tuh_hid_get_report_size(actual_addr)];
            hid_app_request_report(actual_addr, device[actual_addr]);
            ++joy_count;
        }
        
        // Check if this is a Stadia controller and show splash screen
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
    if (!g_llamatron_mode && ui_) {
        ui_->set_mouse_enabled(true);
    }
}

void HidInput::handle_keyboard() {
    std::bitset<128> wheel_press_mask;
    while (!wheel_pulses.empty()) {
        uint8_t pulse_key = wheel_pulses.front();
        wheel_pulses.pop_front();
        if (pulse_key < wheel_press_mask.size()) {
            wheel_press_mask.set(pulse_key);
        }
    }

    bool keyboard_handled = false;

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
                    if (g_llamatron_mode) {
                        show_llamatron_status("Mouse locked", "Disable Llamatron first");
                    } else {
                        ui_->set_mouse_enabled(!ui_->get_mouse_enabled());
                    }
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
            
            // Check for Ctrl+F5 to set relative mouse mode (send 0x08)
            static bool last_mouse_rel_state = false;
            bool f5_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == MOUSE_RELATIVE_KEY) {
                    f5_pressed = true;
                    break;
                }
            }
            
            if (ctrl_pressed && f5_pressed) {
                if (!last_mouse_rel_state) {
#if ENABLE_OLED_DISPLAY
                    // Show visual feedback on OLED
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 20, 15, 2, (char*)"MOUSE");
                    ssd1306_draw_string(&disp, 10, 35, 1, (char*)"Relative Mode");
                    ssd1306_draw_string(&disp, 15, 50, 1, (char*)"Ctrl+F5");
                    ssd1306_show(&disp);
#endif
                    
                    // First disable joystick reporting (0x1A = disable joystick)
                    hd6301_receive_byte(0x1A);
                    hd6301_receive_byte(0x00);  // Disable both joysticks
                    
                    // Enable mouse reporting (0x92 0x00 = enable mouse)
                    hd6301_receive_byte(0x92);
                    hd6301_receive_byte(0x00);  // Enable mouse
                    
                    // Then send 0x08 (SET RELATIVE MOUSE MODE) to HD6301
                    hd6301_receive_byte(0x08);
                    
#if ENABLE_OLED_DISPLAY
                    // Small delay so user can see the message
                    sleep_ms(500);
#endif
                    
                    last_mouse_rel_state = true;
                }
            } else {
                last_mouse_rel_state = false;
            }
            
            // Check for Ctrl+F6 to set absolute mouse mode (send 0x09 + parameters)
            static bool last_mouse_abs_state = false;
            bool f6_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == MOUSE_ABSOLUTE_KEY) {
                    f6_pressed = true;
                    break;
                }
            }
            
            if (ctrl_pressed && f6_pressed) {
                if (!last_mouse_abs_state) {
#if ENABLE_OLED_DISPLAY
                    // Show visual feedback on OLED
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 20, 15, 2, (char*)"MOUSE");
                    ssd1306_draw_string(&disp, 10, 35, 1, (char*)"Absolute Mode");
                    ssd1306_draw_string(&disp, 15, 50, 1, (char*)"Ctrl+F6");
                    ssd1306_show(&disp);
#endif
                    
                    // First disable joystick reporting (0x1A = disable joystick)
                    hd6301_receive_byte(0x1A);
                    hd6301_receive_byte(0x00);  // Disable both joysticks
                    
                    // Enable mouse reporting (0x92 0x00 = enable mouse)
                    hd6301_receive_byte(0x92);
                    hd6301_receive_byte(0x00);  // Enable mouse
                    
                    // Then send 0x09 (SET ABSOLUTE MOUSE MODE) to HD6301
                    // Format: 0x09 Xmax_MSB Xmax_LSB Ymax_MSB Ymax_LSB
                    // Using standard ST high-res: 640x400
                    hd6301_receive_byte(0x09);
                    hd6301_receive_byte(0x02);  // Xmax MSB (640 = 0x0280)
                    hd6301_receive_byte(0x80);  // Xmax LSB
                    hd6301_receive_byte(0x01);  // Ymax MSB (400 = 0x0190)
                    hd6301_receive_byte(0x90);  // Ymax LSB
                    
#if ENABLE_OLED_DISPLAY
                    // Small delay so user can see the message
                    sleep_ms(500);
#endif
                    
                    last_mouse_abs_state = true;
                }
            } else {
                last_mouse_abs_state = false;
            }
            
            // Check for Ctrl+F7 to set mouse keycode mode (send 0x0A + parameters)
            static bool last_mouse_key_state = false;
            bool f7_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == MOUSE_KEYCODE_KEY) {
                    f7_pressed = true;
                    break;
                }
            }
            
            if (ctrl_pressed && f7_pressed) {
                if (!last_mouse_key_state) {
#if ENABLE_OLED_DISPLAY
                    // Show visual feedback on OLED
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 20, 15, 2, (char*)"MOUSE");
                    ssd1306_draw_string(&disp, 10, 35, 1, (char*)"Keycode Mode");
                    ssd1306_draw_string(&disp, 15, 50, 1, (char*)"Ctrl+F7");
                    ssd1306_show(&disp);
#endif
                    
                    // First disable joystick reporting (0x1A = disable joystick)
                    hd6301_receive_byte(0x1A);
                    hd6301_receive_byte(0x00);  // Disable both joysticks
                    
                    // Enable mouse reporting (0x92 0x00 = enable mouse)
                    hd6301_receive_byte(0x92);
                    hd6301_receive_byte(0x00);  // Enable mouse
                    
                    // Then send 0x0A (SET MOUSE KEYCODE MODE) to HD6301
                    // Format: 0x0A deltaX deltaY
                    // Using 1,1 as reasonable defaults (1 pixel per keypress)
                    hd6301_receive_byte(0x0A);
                    hd6301_receive_byte(0x01);  // deltaX = 1
                    hd6301_receive_byte(0x01);  // deltaY = 1
                    
#if ENABLE_OLED_DISPLAY
                    // Small delay so user can see the message
                    sleep_ms(500);
#endif
                    
                    last_mouse_key_state = true;
                }
            } else {
                last_mouse_key_state = false;
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
#if ENABLE_OLED_DISPLAY
                    // Show visual feedback on OLED
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 15, 15, 2, (char*)"JOYSTICK");
                    ssd1306_draw_string(&disp, 30, 35, 1, (char*)"MODE");
                    ssd1306_draw_string(&disp, 15, 50, 1, (char*)"Ctrl+F8");
                    ssd1306_show(&disp);
#endif
                    
                    // Send 0x14 (SET JOYSTICK EVENT REPORTING) to HD6301
                    hd6301_receive_byte(0x14);
                    
#if ENABLE_OLED_DISPLAY
                    // Small delay so user can see the message
                    sleep_ms(500);
#endif
                    
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
#if ENABLE_OLED_DISPLAY
                    // Show visual feedback on OLED
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 30, 20, 2, (char*)"RESET");
                    ssd1306_draw_string(&disp, 20, 45, 1, (char*)"Ctrl+F11");
                    ssd1306_show(&disp);
                    
                    // Small delay so user can see the message
                    sleep_ms(500);
#endif
                    
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

            // Check for Ctrl+F4 to toggle Llamatron dual-stick mode
            static bool last_llama_toggle = false;
            bool f4_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb->keycode[i] == HID_KEY_F4) {
                    f4_pressed = true;
                    break;
                }
            }

            if (ctrl_pressed && f4_pressed) {
                if (!last_llama_toggle) {
                    if (g_llamatron_mode) {
                        g_llamatron_mode = false;
                        g_llamatron_active = false;
                        // Reset pause state when disabling Llamatron mode
                        g_llama_paused = false;
                        g_llama_pause_button_prev = false;
                        key_states[ATARI_KEY_P] = 0;
                        key_states[ATARI_KEY_O] = 0;
                        if (g_llamatron_restore_mouse && ui_) {
                            ui_->set_mouse_enabled(true);
                            g_llamatron_restore_mouse = false;
                        }
                        show_llamatron_status("DISABLED", nullptr);
                    } else {
                        uint8_t joy_setting = ui_->get_joystick();
                        bool joy0_usb = !(joy_setting & 0x01);
                        bool joy1_usb = !(joy_setting & 0x02);
                        uint8_t pad_count = count_connected_gamepads();
                        if (!joy0_usb || !joy1_usb) {
                            show_llamatron_status("USB joysticks only", "Set Joy0/Joy1 to USB");
                        } else if (pad_count != 1) {
                            show_llamatron_status("Requires single pad", "Connect only one gamepad");
                        } else {
                            g_llamatron_mode = true;
                            if (ui_) {
                                g_llamatron_restore_mouse = ui_->get_mouse_enabled();
                                if (g_llamatron_restore_mouse) {
                                    ui_->set_mouse_enabled(false);
                                }
                            } else {
                                g_llamatron_restore_mouse = false;
                            }
                            show_llamatron_status("ACTIVE", nullptr);
                        }
                    }
                    last_llama_toggle = true;
                }
            } else {
                last_llama_toggle = false;
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
                bool pulse_active = wheel_press_mask.test(i);
                bool prev_pulse_active = wheel_prev_mask.test(i);
                
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
                
                // Apply wheel pulses (momentary press)
                // Wheel pulses override keyboard state - if wheel is active, key is pressed
                if (pulse_active) {
                    down = true;
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
            keyboard_handled = true;
        }
    }

    if (!keyboard_handled) {
        // No keyboard present; apply wheel presses directly to key matrix
        // First, set keys that are in the current mask
        for (size_t idx = 0; idx < wheel_press_mask.size(); ++idx) {
            if (wheel_press_mask.test(idx)) {
                key_states[idx] = 1;
            }
        }
        // Then, clear keys that were in previous mask but not in current mask
        // (only clear if they're not being set in this frame)
        for (size_t idx = 0; idx < wheel_prev_mask.size(); ++idx) {
            if (wheel_prev_mask.test(idx) && !wheel_press_mask.test(idx)) {
                key_states[idx] = 0;
            }
        }
        // Update wheel_prev_mask AFTER processing (so keys persist for at least one frame)
        wheel_prev_mask = wheel_press_mask;
    } else if (wheel_prev_mask.any()) {
        // Clear any residual injections from a previous no-keyboard frame
        for (size_t idx = 0; idx < wheel_prev_mask.size(); ++idx) {
            if (wheel_prev_mask.test(idx) && !wheel_press_mask.test(idx)) {
                key_states[idx] = 0;
            }
        }
    }
    
    // Update wheel_prev_mask for next frame (after all processing)
    wheel_prev_mask = wheel_press_mask;
    
    // Handle Bluetooth keyboards
#if ENABLE_BLUEPAD32
    if (bt_runtime_is_enabled()) {
        // Get Bluepad32 keyboard structure (matches uni_keyboard_t)
        // UNI_KEYBOARD_PRESSED_KEYS_MAX is 10, but HID standard is 6 keys
        typedef struct {
            uint8_t modifiers;
            uint8_t pressed_keys[10];  // UNI_KEYBOARD_PRESSED_KEYS_MAX = 10
        } bt_keyboard_t;
        
        bt_keyboard_t bt_kb;
        int bt_kb_count = bluepad32_get_keyboard_count();
        
        // Process first connected Bluetooth keyboard
        if (bt_kb_count > 0) {
            bt_keyboard_t bt_kb;
            // Try to get keyboard data (marks as read)
            bool has_data = bluepad32_get_keyboard(0, &bt_kb);
            
            // If no new data, peek at current state (for shortcuts)
            if (!has_data) {
                has_data = bluepad32_peek_keyboard(0, &bt_kb);
            }
            
            if (has_data) {
                // Convert Bluepad32 keyboard format to TinyUSB format
                hid_keyboard_report_t kb_report;
                kb_report.modifier = bt_kb.modifiers;
                kb_report.reserved = 0;
                
                // Copy pressed keys (up to 6 keys for HID standard)
                int key_count = 0;
                for (int i = 0; i < 10 && key_count < 6; i++) {  // UNI_KEYBOARD_PRESSED_KEYS_MAX = 10
                    if (bt_kb.pressed_keys[i] != 0) {
                        kb_report.keycode[key_count++] = bt_kb.pressed_keys[i];
                    }
                }
                // Zero out remaining slots
                for (int i = key_count; i < 6; i++) {
                    kb_report.keycode[i] = 0;
                }
                
                // Process the keyboard report using the same logic as USB keyboards
                // (reuse the existing keyboard handling code by treating it as a USB keyboard)
                // For now, we'll process modifiers and keycodes directly
                // Check for Ctrl+F12 to toggle mouse mode
                static bool last_bt_toggle_state = false;
                bool ctrl_pressed = (kb_report.modifier & KEYBOARD_MODIFIER_LEFTCTRL) || (kb_report.modifier & KEYBOARD_MODIFIER_RIGHTCTRL);
                bool f12_pressed = false;
                bool f5_pressed = false;
                bool f6_pressed = false;
                bool f7_pressed = false;
                bool f9_pressed = false;
                bool f10_pressed = false;
                bool f11_pressed = false;
                bool f4_pressed = false;
                
                for (int i = 0; i < 6; ++i) {
                    if (kb_report.keycode[i] == TOGGLE_MOUSE_MODE) {
                        f12_pressed = true;
                    }
                    if (kb_report.keycode[i] == MOUSE_RELATIVE_KEY) {
                        f5_pressed = true;
                    }
                    if (kb_report.keycode[i] == MOUSE_ABSOLUTE_KEY) {
                        f6_pressed = true;
                    }
                    if (kb_report.keycode[i] == MOUSE_KEYCODE_KEY) {
                        f7_pressed = true;
                    }
                    if (kb_report.keycode[i] == HID_KEY_F9) {
                        f9_pressed = true;
                    }
                    if (kb_report.keycode[i] == HID_KEY_F10) {
                        f10_pressed = true;
                    }
                    if (kb_report.keycode[i] == XRESET_KEY) {
                        f11_pressed = true;
                    }
                    if (kb_report.keycode[i] == HID_KEY_F4) {
                        f4_pressed = true;
                    }
                }
            if (ctrl_pressed && f12_pressed) {
                if (!last_bt_toggle_state) {
                    if (g_llamatron_mode) {
                        show_llamatron_status("Mouse locked", "Disable Llamatron first");
                    } else {
                        ui_->set_mouse_enabled(!ui_->get_mouse_enabled());
                    }
                    last_bt_toggle_state = true;
                }
            } else {
                last_bt_toggle_state = false;
            }
            
            // Check for Ctrl+F5 to set relative mouse mode (send 0x08)
            static bool last_bt_mouse_rel_state = false;
            
            if (ctrl_pressed && f5_pressed) {
                if (!last_bt_mouse_rel_state) {
#if ENABLE_OLED_DISPLAY
                    // Show visual feedback on OLED
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 20, 15, 2, (char*)"MOUSE");
                    ssd1306_draw_string(&disp, 10, 35, 1, (char*)"Relative Mode");
                    ssd1306_draw_string(&disp, 15, 50, 1, (char*)"Ctrl+F5");
                    ssd1306_show(&disp);
#endif
                    
                    // First disable joystick reporting (0x1A = disable joystick)
                    hd6301_receive_byte(0x1A);
                    hd6301_receive_byte(0x00);  // Disable both joysticks
                    
                    // Enable mouse reporting (0x92 0x00 = enable mouse)
                    hd6301_receive_byte(0x92);
                    hd6301_receive_byte(0x00);  // Enable mouse
                    
                    // Then send 0x08 (SET RELATIVE MOUSE MODE) to HD6301
                    hd6301_receive_byte(0x08);
                    
#if ENABLE_OLED_DISPLAY
                    // Small delay so user can see the message
                    sleep_ms(500);
#endif
                    
                    last_bt_mouse_rel_state = true;
                }
            } else {
                last_bt_mouse_rel_state = false;
            }
            
            // Check for Ctrl+F6 to set absolute mouse mode (send 0x09 + parameters)
            static bool last_bt_mouse_abs_state = false;
            
            if (ctrl_pressed && f6_pressed) {
                if (!last_bt_mouse_abs_state) {
#if ENABLE_OLED_DISPLAY
                    // Show visual feedback on OLED
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 20, 15, 2, (char*)"MOUSE");
                    ssd1306_draw_string(&disp, 10, 35, 1, (char*)"Absolute Mode");
                    ssd1306_draw_string(&disp, 15, 50, 1, (char*)"Ctrl+F6");
                    ssd1306_show(&disp);
#endif
                    
                    // First disable joystick reporting (0x1A = disable joystick)
                    hd6301_receive_byte(0x1A);
                    hd6301_receive_byte(0x00);  // Disable both joysticks
                    
                    // Enable mouse reporting (0x92 0x00 = enable mouse)
                    hd6301_receive_byte(0x92);
                    hd6301_receive_byte(0x00);  // Enable mouse
                    
                    // Then send 0x09 (SET ABSOLUTE MOUSE MODE) to HD6301
                    // Format: 0x09 Xmax_MSB Xmax_LSB Ymax_MSB Ymax_LSB
                    // Using standard ST high-res: 640x400
                    hd6301_receive_byte(0x09);
                    hd6301_receive_byte(0x02);  // Xmax MSB (640 = 0x0280)
                    hd6301_receive_byte(0x80);  // Xmax LSB
                    hd6301_receive_byte(0x01);  // Ymax MSB (400 = 0x0190)
                    hd6301_receive_byte(0x90);  // Ymax LSB
                    
#if ENABLE_OLED_DISPLAY
                    // Small delay so user can see the message
                    sleep_ms(500);
#endif
                    
                    last_bt_mouse_abs_state = true;
                }
            } else {
                last_bt_mouse_abs_state = false;
            }
            
            // Check for Ctrl+F7 to set mouse keycode mode (send 0x0A + parameters)
            static bool last_bt_mouse_key_state = false;
            
            if (ctrl_pressed && f7_pressed) {
                if (!last_bt_mouse_key_state) {
#if ENABLE_OLED_DISPLAY
                    // Show visual feedback on OLED
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 20, 15, 2, (char*)"MOUSE");
                    ssd1306_draw_string(&disp, 10, 35, 1, (char*)"Keycode Mode");
                    ssd1306_draw_string(&disp, 15, 50, 1, (char*)"Ctrl+F7");
                    ssd1306_show(&disp);
#endif
                    
                    // First disable joystick reporting (0x1A = disable joystick)
                    hd6301_receive_byte(0x1A);
                    hd6301_receive_byte(0x00);  // Disable both joysticks
                    
                    // Enable mouse reporting (0x92 0x00 = enable mouse)
                    hd6301_receive_byte(0x92);
                    hd6301_receive_byte(0x00);  // Enable mouse
                    
                    // Then send 0x0A (SET MOUSE KEYCODE MODE) to HD6301
                    // Format: 0x0A deltaX deltaY
                    // Using 1,1 as reasonable defaults (1 pixel per keypress)
                    hd6301_receive_byte(0x0A);
                    hd6301_receive_byte(0x01);  // deltaX = 1
                    hd6301_receive_byte(0x01);  // deltaY = 1
                    
#if ENABLE_OLED_DISPLAY
                    // Small delay so user can see the message
                    sleep_ms(500);
#endif
                    
                    last_bt_mouse_key_state = true;
                }
            } else {
                last_bt_mouse_key_state = false;
            }
            
            // Check for Ctrl+F11 to trigger XRESET (HD6301 hardware reset)
            static bool last_bt_reset_state = false;
            if (ctrl_pressed && f11_pressed) {
                if (!last_bt_reset_state) {
#if ENABLE_OLED_DISPLAY
                    // Show visual feedback on OLED
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 30, 20, 2, (char*)"RESET");
                    ssd1306_draw_string(&disp, 20, 45, 1, (char*)"Ctrl+F11");
                    ssd1306_show(&disp);
                    
                    // Small delay so user can see the message
                    sleep_ms(500);
#endif
                    
                    // Trigger the reset
                    hd6301_trigger_reset();
                    last_bt_reset_state = true;
                }
            } else {
                last_bt_reset_state = false;
            }
            
            // Check for Ctrl+F9 to toggle Joystick 0 (D-SUB <-> USB)
            static bool last_bt_joy0_state = false;
            if (ctrl_pressed && f9_pressed) {
                if (!last_bt_joy0_state) {
                    ui_->toggle_joystick_source(0);  // Toggle Joystick 0
                    last_bt_joy0_state = true;
                }
            } else {
                last_bt_joy0_state = false;
            }
            
            // Check for Ctrl+F10 to toggle Joystick 1 (D-SUB <-> USB)
            static bool last_bt_joy1_state = false;
            if (ctrl_pressed && f10_pressed) {
                if (!last_bt_joy1_state) {
                    ui_->toggle_joystick_source(1);  // Toggle Joystick 1
                    last_bt_joy1_state = true;
                }
            } else {
                last_bt_joy1_state = false;
            }
            
            // Check for Ctrl+F4 to toggle Llamatron dual-stick mode
            static bool last_bt_llama_toggle = false;
            if (ctrl_pressed && f4_pressed) {
                if (!last_bt_llama_toggle) {
                    if (g_llamatron_mode) {
                        g_llamatron_mode = false;
                        g_llamatron_active = false;
                        // Reset pause state when disabling Llamatron mode
                        g_llama_paused = false;
                        g_llama_pause_button_prev = false;
                        key_states[ATARI_KEY_P] = 0;
                        key_states[ATARI_KEY_O] = 0;
                        if (g_llamatron_restore_mouse && ui_) {
                            ui_->set_mouse_enabled(true);
                            g_llamatron_restore_mouse = false;
                        }
                        show_llamatron_status("DISABLED", nullptr);
                    } else {
                        uint8_t joy_setting = ui_->get_joystick();
                        bool joy0_usb = !(joy_setting & 0x01);
                        bool joy1_usb = !(joy_setting & 0x02);
                        uint8_t pad_count = count_connected_gamepads();
                        if (!joy0_usb || !joy1_usb) {
                            show_llamatron_status("USB joysticks only", "Set Joy0/Joy1 to USB");
                        } else if (pad_count != 1) {
                            show_llamatron_status("Requires single pad", "Connect only one gamepad");
                        } else {
                            g_llamatron_mode = true;
                            if (ui_) {
                                g_llamatron_restore_mouse = ui_->get_mouse_enabled();
                                if (g_llamatron_restore_mouse) {
                                    ui_->set_mouse_enabled(false);
                                }
                            } else {
                                g_llamatron_restore_mouse = false;
                            }
                            show_llamatron_status("ACTIVE", nullptr);
                        }
                    }
                    last_bt_llama_toggle = true;
                }
            } else {
                last_bt_llama_toggle = false;
            }
            
            // Check for Alt modifier (needed for ALT + /, ALT + [, ALT + ] shortcuts)
            bool alt_pressed = (kb_report.modifier & KEYBOARD_MODIFIER_LEFTALT) || 
                              (kb_report.modifier & KEYBOARD_MODIFIER_RIGHTALT);
            
            // Check for Alt + Plus to set 270MHz
            static bool last_bt_plus_state = false;
            bool plus_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb_report.keycode[i] == HID_KEY_EQUAL) {
                    plus_pressed = true;
                    break;
                }
            }
            if (alt_pressed && plus_pressed) {
                if (!last_bt_plus_state) {
                    set_sys_clock_khz(270000, false);
                    last_bt_plus_state = true;
                }
            } else {
                last_bt_plus_state = false;
            }
            
            // Check for Alt + Minus to set 150MHz
            static bool last_bt_minus_state = false;
            bool minus_pressed = false;
            for (int i = 0; i < 6; ++i) {
                if (kb_report.keycode[i] == HID_KEY_MINUS) {
                    minus_pressed = true;
                    break;
                }
            }
            if (alt_pressed && minus_pressed) {
                if (!last_bt_minus_state) {
                    set_sys_clock_khz(150000, false);
                    last_bt_minus_state = true;
                }
            } else {
                last_bt_minus_state = false;
            }
            
            // Convert to Atari ST keycodes and update key_states
            unsigned char st_keys[6] = {0};
            for (int i = 0; i < 6; ++i) {
                if (kb_report.keycode[i] != 0) {
                    // If Alt + / is pressed, replace / with INSERT (matching USB keyboard behavior)
                    if (alt_pressed && kb_report.keycode[i] == HID_KEY_SLASH) {
                        st_keys[i] = ATARI_INSERT;
                    }
                    // If Alt + [ is pressed, send Atari keypad / (scancode 101)
                    else if (alt_pressed && kb_report.keycode[i] == HID_KEY_BRACKET_LEFT) {
                        st_keys[i] = 101;  // Atari keypad /
                    }
                    // If Alt + ] is pressed, send Atari keypad * (scancode 102)
                    else if (alt_pressed && kb_report.keycode[i] == HID_KEY_BRACKET_RIGHT) {
                        st_keys[i] = 102;  // Atari keypad *
                    }
                    // If Alt + Plus or Alt + Minus, don't send to Atari (used for clock control)
                    else if (alt_pressed && (kb_report.keycode[i] == HID_KEY_EQUAL || kb_report.keycode[i] == HID_KEY_MINUS)) {
                        st_keys[i] = 0;
                    }
                    // If Ctrl+F11, don't send to Atari (used for XRESET)
                    else if (ctrl_pressed && kb_report.keycode[i] == XRESET_KEY) {
                        st_keys[i] = 0;
                    }
                    // If Ctrl+F10, don't send to Atari (used for Joy1 toggle)
                    else if (ctrl_pressed && kb_report.keycode[i] == HID_KEY_F10) {
                        st_keys[i] = 0;
                    }
                    // If Ctrl+F9, don't send to Atari (used for Joy0 toggle)
                    else if (ctrl_pressed && kb_report.keycode[i] == HID_KEY_F9) {
                        st_keys[i] = 0;
                    }
                    // All other keys (including cursor keys) work normally with ALT
                    else {
                        st_keys[i] = st_key_lookup_hid_gb[kb_report.keycode[i]];
                    }
                } else {
                    st_keys[i] = 0;
                }
            }
            
            // Update key states
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
                key_states[ATARI_LSHIFT] = (kb_report.modifier & KEYBOARD_MODIFIER_LEFTSHIFT) ? 1 : 0;
                key_states[ATARI_RSHIFT] = (kb_report.modifier & KEYBOARD_MODIFIER_RIGHTSHIFT) ? 1 : 0;
                key_states[ATARI_CTRL] = ((kb_report.modifier & KEYBOARD_MODIFIER_LEFTCTRL) ||
                                          (kb_report.modifier & KEYBOARD_MODIFIER_RIGHTCTRL)) ? 1 : 0;
                key_states[ATARI_ALT] = ((kb_report.modifier & KEYBOARD_MODIFIER_LEFTALT) ||
                                          (kb_report.modifier & KEYBOARD_MODIFIER_RIGHTALT)) ? 1 : 0;
            }
        }
    }
#endif
}

void HidInput::handle_mouse(const int64_t cpu_cycles) {
    int32_t x = 0;
    int32_t y = 0;
    
    
    for (auto it : device) {
        // Decode if this is a multi-interface mouse (key > 128)
        uint8_t actual_addr = (it.first >= 128) ? (it.first - 128) : it.first;
        
        if (tuh_hid_get_type(it.first) != HID_MOUSE) {
            continue;
        }
        
        
        if (tuh_hid_is_mounted(it.first) && !tuh_hid_is_busy(it.first)) {  // Use key, not actual_addr
            hid_mouse_report_t* mouse = (hid_mouse_report_t*)it.second;

            const uint8_t* js = it.second;
            HID_ReportInfo_t* info = tuh_hid_get_report_info(it.first);  // Use key
            
            // Check if this is a multi-interface mouse (Logitech Unifying)
            // These have key >= 128 AND are boot protocol mice (have simple 4-byte reports)
            bool is_multi_interface_mouse = (it.first >= 128);
            int wheel_delta = 0;
            
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

                if (mouse) {
                    wheel_delta = mouse->wheel;
                }
            }
            else if (info) {
                // Standard HID parser for regular mice
                int8_t buttons = 0;
                int wheel_candidate = 0;
                bool wheel_found = false;

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
                    else if ((item->Attributes.Usage.Page == USAGE_PAGE_GENERIC_DCTRL) &&
                             (item->Attributes.Usage.Usage == 0x38) &&
                             (item->ItemType == HID_REPORT_ITEM_In)) {
                        wheel_candidate = GET_I32_VALUE(item);
                        wheel_found = true;
                    }
                    else if ((item->Attributes.Usage.Page == 0x0C) &&
                             (item->Attributes.Usage.Usage == 0x0238) &&
                             (item->ItemType == HID_REPORT_ITEM_In)) {
                        // Consumer Control AC Pan can be used for wheels on some devices
                        wheel_candidate = GET_I32_VALUE(item);
                        wheel_found = true;
                    }
                }
                // Update button state
                mouse_state = (mouse_state & 0xfd) | ((buttons & MOUSE_BUTTON_LEFT) ? 2 : 0);
                mouse_state = (mouse_state & 0xfe) | ((buttons & MOUSE_BUTTON_RIGHT) ? 1 : 0);

                if (wheel_found) {
                    wheel_delta = wheel_candidate;
                } else if (mouse) {
                    wheel_delta = mouse->wheel;
                }
            } else if (mouse) {
                wheel_delta = mouse->wheel;
            }

            if (wheel_delta != 0) {
                enqueue_wheel_pulses(wheel_delta);
            }
            // Trigger the next report (use device key, not actual_addr, for multi-interface devices)
            hid_app_request_report(it.first, it.second);
        }
    }
    // Handle Bluetooth mice
#if ENABLE_BLUEPAD32
    if (bt_runtime_is_enabled()) {
        // Use uni_mouse_t structure exactly as defined in bluepad32
        // Structure: int32_t delta_x, int32_t delta_y, uint16_t buttons, int8_t scroll_wheel, uint8_t misc_buttons
        // We define it locally to avoid header dependencies, but it must match exactly
        typedef struct {
            int32_t delta_x;
            int32_t delta_y;
            uint16_t buttons;  // Bitmask: UNI_MOUSE_BUTTON_LEFT = BIT(0) = 0x01, UNI_MOUSE_BUTTON_RIGHT = BIT(1) = 0x02
            int8_t scroll_wheel;
            uint8_t misc_buttons;
        } __attribute__((packed)) bt_mouse_t;
        
        bt_mouse_t bt_mouse;
        int bt_mouse_count = bluepad32_get_mouse_count();
        
        // Process first connected Bluetooth mouse
        if (bt_mouse_count > 0 && bluepad32_get_mouse(0, &bt_mouse)) {
            // Extract movement deltas (Bluepad32 clamps to -127 to 127)
            // Use deltas directly (matching Logronoid's approach) - acceleration will be applied later
            int32_t bt_x = bt_mouse.delta_x;
            int32_t bt_y = bt_mouse.delta_y;
            
            // Accumulate with USB mouse movement
            x += bt_x;
            y += bt_y;
            
            // Extract button states using bitmasks (matching logronoid's approach)
            // UNI_MOUSE_BUTTON_LEFT = BIT(0) = 0x01
            // UNI_MOUSE_BUTTON_RIGHT = BIT(1) = 0x02
            bool left_down = (bt_mouse.buttons & 0x01) != 0;
            bool right_down = (bt_mouse.buttons & 0x02) != 0;
            
            // Update button state (match USB mouse button handling)
            // Atari ST mouse_state: bit 1 = left button (0x02), bit 0 = right button (0x01)
            mouse_state = (mouse_state & 0xfd) | (left_down ? 2 : 0);   // Left button
            mouse_state = (mouse_state & 0xfe) | (right_down ? 1 : 0);  // Right button
            
            // Handle scroll wheel (matching USB mouse behavior)
            // Note: scroll_wheel is int8_t, so values are -128 to 127
            // Positive = scroll down, negative = scroll up
            if (bt_mouse.scroll_wheel != 0) {
                enqueue_wheel_pulses(bt_mouse.scroll_wheel);
            }
        }
    }
#endif
    
    // Handle the mouse acceleration/deceleration configured in the UI.
    // Base multiplier of 1.0 with speed adjustment (0.0-9.0) adds 0.0-0.9
    // This gives a range of 1.0x to 1.9x acceleration
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
                    #if ENABLE_STADIA_DEBUG
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

bool HidInput::get_ps3_joystick(int joystick_num, uint8_t& axis, uint8_t& button) {
    // Get PS3 controller state
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        ps3_controller_t* ps3 = ps3_get_controller(dev_addr);
        if (ps3 && ps3->connected) {
            // Found a connected PS3 controller!
            ps3_to_atari(ps3, joystick_num, &axis, &button);
            return true;
        }
    }
    return false;
}

bool HidInput::get_gamecube_joystick(int joystick_num, uint8_t& axis, uint8_t& button) {
    // Get GameCube adapter state via HID path (v11.2+: HID hijacking with fixed bit structure)
    static uint32_t debug_count = 0;
    debug_count++;
    
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        gc_adapter_t* gc = gc_get_adapter(dev_addr);
        
        if (gc && gc->connected && gc->active_port != 0xFF) {
            // Found a connected GameCube controller!
            
#if ENABLE_SERIAL_LOGGING
            // Debug first few calls
            if (debug_count <= 3) {
                printf("GC: get_gamecube_joystick() - FOUND adapter at addr=%d, port=%d\n", 
                       dev_addr, gc->active_port);
            }
#endif
            
            gc_to_atari(gc, joystick_num, &axis, &button);
            
#if ENABLE_SERIAL_LOGGING
            // Debug conversion result
            if (debug_count <= 3) {
                printf("GC: gc_to_atari() returned axis=0x%02X, button=%d\n", axis, button);
            }
#endif
            
            return true;
        }
    }
    
#if ENABLE_SERIAL_LOGGING
    // Debug if not found
    if (debug_count == 1) {
        printf("GC: get_gamecube_joystick() - NO adapter found\n");
    }
#endif
    
    return false;
}

bool HidInput::get_ps4_joystick(int joystick_num, uint8_t& axis, uint8_t& button) {
    // Get PS4 controller state
    static uint32_t call_count = 0;
    static uint32_t success_count = 0;
    call_count++;
    
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        ps4_controller_t* ps4 = ps4_get_controller(dev_addr);
        if (ps4 && ps4->connected) {
            // Found a connected PS4 controller!
            ps4_to_atari(ps4, joystick_num, &axis, &button);
            
            success_count++;
            // Debug: Show input detection (throttled to avoid spam)
            if ((call_count % 100) == 0 || (success_count <= 5)) {
                printf("PS4 Joy%d: INPUT DETECTED - axis=0x%02X button=%d (calls=%lu success=%lu)\n", 
                       joystick_num, axis, button, call_count, success_count);
            }
            
            return true;
        }
    }
    
    
    return false;
}

bool HidInput::get_ps5_joystick(int joystick_num, uint8_t& axis, uint8_t& button) {
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        ps5_controller_t* ps5 = ps5_get_controller(dev_addr);
        if (ps5 && ps5->connected) {
            ps5_to_atari(ps5, joystick_num, &axis, &button);
            return true;
        }
    }
    return false;
}

bool HidInput::get_psc_joystick(int joystick_num, uint8_t& axis, uint8_t& button) {
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        psc_controller_t* psc = psc_get_controller(dev_addr);
        if (psc && psc->connected) {
            psc_to_atari(psc, joystick_num, &axis, &button);
            return true;
        }
    }
    return false;
}

bool HidInput::get_horipad_joystick(int joystick_num, uint8_t& axis, uint8_t& button) {
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        horipad_controller_t* hp = horipad_get_controller(dev_addr);
        if (hp && hp->connected) {
            horipad_to_atari(hp, joystick_num, &axis, &button);
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
    // Find the joystick addresses (USB mode only)
    std::vector<int> joystick_addr;
    int next_joystick = 0;
    // Scan for USB HID joysticks if USB is enabled at runtime
    // (Works even when Bluetooth is compiled in)
    if (usb_runtime_is_enabled()) {
        for (auto it : device) {
            if (tuh_hid_get_type(it.first) == HID_JOYSTICK) {
                joystick_addr.push_back(it.first);
            }
        }
    }

    bool llama_prev_active = g_llamatron_active;
    // Llamatron mode supported for both USB and Bluetooth gamepads
    if (g_llamatron_mode) {
        uint8_t joy_setting = ui_->get_joystick();
        bool usb0 = !(joy_setting & 0x01);
        bool usb1 = !(joy_setting & 0x02);
        uint8_t pad_count = count_connected_gamepads();
        uint8_t axis1 = 0, fire1 = 0, axis0 = 0, fire0 = 0;
        bool have_sample = (pad_count == 1 && usb0 && usb1 &&
                            collect_llamatron_sample(axis1, fire1, axis0, fire0));

        if (have_sample) {
            g_llamatron_active = true;
            g_llama_axis_joy1 = axis1;
            g_llama_fire_joy1 = fire1;
            g_llama_axis_joy0 = axis0;
            g_llama_fire_joy0 = fire0;

            // Handle pause/unpause button in Llamatron mode
            bool pause_button_pressed = check_llamatron_pause_button();
            if (pause_button_pressed && !g_llama_pause_button_prev) {
                // Button just pressed (edge detection)
                // Toggle pause state and inject appropriate key
                if (g_llama_paused) {
                    // Currently paused, send 'O' to unpause
                    key_states[ATARI_KEY_O] = 1;
                    g_llama_paused = false;
                } else {
                    // Currently unpaused, send 'P' to pause
                    key_states[ATARI_KEY_P] = 1;
                    g_llama_paused = true;
                }
            } else if (!pause_button_pressed && g_llama_pause_button_prev) {
                // Button just released, clear the key
                key_states[ATARI_KEY_P] = 0;
                key_states[ATARI_KEY_O] = 0;
            }
            g_llama_pause_button_prev = pause_button_pressed;

        } else {
            g_llamatron_active = false;
            g_llama_axis_joy1 = 0;
            g_llama_fire_joy1 = 0;
            g_llama_axis_joy0 = 0;
            g_llama_fire_joy0 = 0;
            // Clear pause button state when Llamatron is inactive
            g_llama_pause_button_prev = false;
            key_states[ATARI_KEY_P] = 0;
            key_states[ATARI_KEY_O] = 0;


            if (g_llamatron_mode && llama_prev_active) {
                if (pad_count != 1) {
                    show_llamatron_status("Suspended", "Need single pad");
                } else if (!usb0 || !usb1) {
                    show_llamatron_status("Suspended", "Joy0/Joy1 must use USB");
                }
            }
        }
    } else {
        g_llamatron_active = false;
        g_llama_axis_joy1 = 0;
        g_llama_fire_joy1 = 0;
        g_llama_axis_joy0 = 0;
        g_llama_fire_joy0 = 0;
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

            if (g_llamatron_active) {
                if (joystick == 1) {
                    axis = g_llama_axis_joy1;
                    button = g_llama_fire_joy1;
                    got_input = true;
                } else if (joystick == 0) {
                    axis = g_llama_axis_joy0;
                    button = g_llama_fire_joy0;
                    got_input = true;
                }
            }
            
            // Check USB controllers if USB is enabled at runtime
            // (Works even when Bluetooth is compiled in)
            if (usb_runtime_is_enabled()) {
                // USB mode: Check USB HID joystick and USB-specific controllers
                // First priority: USB HID joystick
                if (!got_input && next_joystick < joystick_addr.size()) {
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
                
                // PS5 DualSense (check after PS4)
                if (!got_input && get_ps5_joystick(joystick, axis, button)) {
                    got_input = true;
                }
                
                // PlayStation Classic (check after PS5)
                if (!got_input && get_psc_joystick(joystick, axis, button)) {
                    got_input = true;
                }
                
                // PS3 controller (check after PSC)
                if (!got_input && get_ps3_joystick(joystick, axis, button)) {
                    got_input = true;
                }
                
                // GameCube adapter (check after PS3)
                if (!got_input && get_gamecube_joystick(joystick, axis, button)) {
                    got_input = true;
                }
                
                // Third priority: Switch controller
                if (!got_input && get_switch_joystick(joystick, axis, button)) {
                    got_input = true;
                    g_switch_success++;  // Track Switch success
                }
                
                // HORI HORIPAD (Switch) - check after Switch
                if (!got_input && get_horipad_joystick(joystick, axis, button)) {
                    got_input = true;
                }
                
                // Fourth priority: Stadia controller - NOW USES GENERIC HID PATH
                // (Stadia is detected as standard HID joystick, handled above)
                
                // Fifth priority: Xbox controller (always check as final fallback)
                // FIX: This ensures Xbox is checked even if stale HID entries exist
                if (!got_input && get_xbox_joystick(joystick, axis, button)) {
                    got_input = true;
                    g_xbox_success++;  // Track Xbox success
                }
            }
            
#if ENABLE_BLUEPAD32
            // Check Bluetooth controllers if Bluetooth is enabled at runtime
            // Match USB behavior: assign to joystick 1 first, then joystick 0
            // This allows mouse to work with joystick 1 (joystick 0 conflicts with mouse)
            if (!got_input && bt_runtime_is_enabled()) {
                int bt_count = bluepad32_get_connected_count();
                if (bt_count > 0) {
                    // Map joystick number: joystick 1 -> bt_index 0, joystick 0 -> bt_index 1
                    // This matches USB behavior where first USB joystick goes to joystick 1
                    int bt_index = (joystick == 1) ? 0 : 1;
                    if (bt_index < bt_count) {
                        // Local struct with layout matching uni_gamepad_t exactly.
                        // See bluepad32's uni_gamepad_t definition.
                        struct {
                            uint8_t  dpad;
                            int32_t  axis_x;
                            int32_t  axis_y;
                            int32_t  axis_rx;
                            int32_t  axis_ry;
                            int32_t  brake;
                            int32_t  throttle;
                            uint16_t buttons;
                            uint8_t  misc_buttons;
                            int32_t  gyro[3];
                            int32_t  accel[3];
                        } bt_gamepad;
                        
                        if (bluepad32_get_gamepad(bt_index, &bt_gamepad)) {
                            uint8_t bt_axis = 0;
                            uint8_t bt_button = 0;
                            if (bluepad32_to_atari_joystick(&bt_gamepad, &bt_axis, &bt_button)) {
                                axis = bt_axis;
                                button = bt_button;
                                got_input = true;
                            }
                        }
                    }
                }
            }
#endif
            
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

#if ENABLE_BLUEPAD32
                    // Special-case for Bluetooth-only mode:
                    // Many Atari games read Joy0 as the primary joystick.
                    // When running in pure Bluetooth mode (USB disabled), mirror
                    // joystick 1 input into joystick 0 as long as mouse is not enabled.
                    if (!usb_runtime_is_enabled() && bt_runtime_is_enabled() && !ui_->get_mouse_enabled()) {
                        joystick_state &= ~0xf;
                        joystick_state |= axis;
                        // Note: we intentionally do NOT mirror the mouse_state buttons here
                        // to avoid interfering with Atari mouse emulation on Joy0.
                    }
#endif
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

