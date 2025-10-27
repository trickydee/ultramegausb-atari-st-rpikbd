/*
 * Atari ST RP2040 IKBD Emulator - Nintendo Switch Controller Support
 * Copyright (C) 2025
 * 
 * Nintendo Switch controller implementation
 * Based on TinyUSB HID controller example
 */

#include "switch_controller.h"
#include "tusb.h"
#include "ssd1306.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ssd1306_t disp;

// Controller storage
static switch_controller_t controllers[MAX_SWITCH_CONTROLLERS];

// Latest values for OLED display (accessible from UI)
static uint16_t last_buttons = 0;
static uint8_t last_dpad = 0;
static int16_t last_lx = 0;
static int16_t last_ly = 0;
static uint8_t last_atari_dir = 0;
static uint8_t last_atari_fire = 0;
static uint32_t global_report_count = 0;
static uint8_t last_report_bytes[12] = {0};  // Store bytes 3-11 for display
static uint16_t last_report_len = 0;

// State tracking for delayed Pro Controller initialization
static bool pro_needs_init = false;
static uint8_t pro_dev_addr = 0;
static uint32_t pro_mount_time = 0;
static bool pro_init_attempted = false;
static uint8_t global_count = 0; // Packet counter for subcommands
static uint16_t pro_report_len_before = 0;
static uint16_t pro_report_len_after = 0;
static bool pro_init_complete = false;

// Track command success for debugging
static uint8_t init_cmd_success = 0;  // Bitmask: bits 0-6 for each of 7 commands

#define PRO_INIT_DELAY_MS 1000  // Wait 1 second after mount before initializing

void switch_get_debug_values(uint16_t* buttons, uint8_t* dpad, int16_t* lx, int16_t* ly,
                              uint8_t* atari_dir, uint8_t* atari_fire) {
    *buttons = last_buttons;
    *dpad = last_dpad;
    *lx = last_lx;
    *ly = last_ly;
    *atari_dir = last_atari_dir;
    *atari_fire = last_atari_fire;
}

uint32_t switch_get_report_count(void) {
    return global_report_count;
}

void switch_get_pro_init_status(bool* attempted, bool* complete, uint16_t* len_before, uint16_t* len_after) {
    *attempted = pro_init_attempted;
    *complete = pro_init_complete;
    *len_before = pro_report_len_before;
    *len_after = pro_report_len_after;
}

uint8_t switch_get_init_cmd_success() {
    return init_cmd_success;
}

// Get detailed debug info for Pro Controller init
uint32_t switch_get_pro_init_elapsed() {
    if (pro_mount_time == 0) return 0;
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    return current_time - pro_mount_time;
}

bool switch_get_pro_init_scheduled() {
    return pro_needs_init;
}

void switch_get_raw_bytes(uint8_t* bytes, uint16_t* len) {
    *len = last_report_len;
    for (int i = 0; i < 9; i++) {
        bytes[i] = last_report_bytes[i];
    }
}

// Check and perform delayed Pro Controller initialization
// This is called from the main loop, NOT from report processing
void switch_check_delayed_init(void) {
    if (!pro_needs_init || pro_init_attempted) {
        return;  // Nothing to do
    }
    
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    uint32_t elapsed = current_time - pro_mount_time;
    
    if (elapsed >= PRO_INIT_DELAY_MS) {
        pro_report_len_before = 7;  // Assume Simple HID mode (7 bytes)
        pro_init_attempted = true;
        pro_needs_init = false;  // Don't try again
        
        // Now send initialization commands
        switch_init_pro_controller(pro_dev_addr);
    }
}

// Allocate a controller slot
static switch_controller_t* allocate_controller(uint8_t dev_addr) {
    for (int i = 0; i < MAX_SWITCH_CONTROLLERS; i++) {
        if (!controllers[i].connected) {
            memset(&controllers[i], 0, sizeof(switch_controller_t));
            controllers[i].dev_addr = dev_addr;
            controllers[i].connected = true;
            controllers[i].deadzone = 20;  // Default deadzone (~15%)
            return &controllers[i];
        }
    }
    return NULL;
}

// Free a controller slot
static void free_controller(uint8_t dev_addr) {
    for (int i = 0; i < MAX_SWITCH_CONTROLLERS; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected) {
            memset(&controllers[i], 0, sizeof(switch_controller_t));
            return;
        }
    }
}

bool switch_is_controller(uint16_t vid, uint16_t pid) {
    // Official Nintendo controllers
    if (vid == SWITCH_VENDOR_ID) {
        switch (pid) {
            case SWITCH_PRO_CONTROLLER:
            case SWITCH_JOYCON_L:
            case SWITCH_JOYCON_R:
            case SWITCH_JOYCON_PAIR:
                return true;
        }
    }
    
    // PowerA third-party controllers
    if (vid == POWERA_VENDOR_ID) {
        switch (pid) {
            case POWERA_FUSION_ARCADE:
            case POWERA_WIRED_PLUS:
            case POWERA_WIRELESS:
                return true;
        }
    }
    
    return false;
}

switch_controller_t* switch_get_controller(uint8_t dev_addr) {
    for (int i = 0; i < MAX_SWITCH_CONTROLLERS; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected) {
            return &controllers[i];
        }
    }
    return NULL;
}

void switch_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len) {
    if (!report || len == 0) return;
    
    switch_controller_t* ctrl = switch_get_controller(dev_addr);
    if (!ctrl) return;
    
    // Increment global report counter
    global_report_count++;
    
    // Save report info for OLED display
    last_report_len = len;
    if (len >= 12) {
        // Save bytes 3-11 (button and stick data)
        for (int i = 0; i < 9 && (i + 3) < len; i++) {
            last_report_bytes[i] = report[i + 3];
        }
    }
    
    // Debug: Log report length after initialization completes
    if (pro_init_attempted && !pro_init_complete) {
        pro_report_len_after = len;
        pro_init_complete = true;
        
#if ENABLE_SWITCH_DEBUG
        printf("Switch: First report AFTER init - length: %d bytes, Report ID: 0x%02X\n", len, report[0]);
        if (len != pro_report_len_before) {
            printf("        !!! REPORT LENGTH CHANGED from %d to %d bytes !!!\n", pro_report_len_before, len);
        }
        printf("        First 16 bytes: ");
        for (int i = 0; i < (len < 16 ? len : 16); i++) {
            printf("%02X ", report[i]);
        }
        printf("\n");
#endif
    }
    
#if ENABLE_SWITCH_DEBUG
    // Debug: Log reports to diagnose static values
    static uint32_t report_count = 0;
    static uint16_t last_report_btns = 0;
    static uint8_t last_report_x = 128;
    static uint8_t last_report_y = 128;
    report_count++;
    
    // Log first 20 reports after init, or whenever values change
    bool should_log = false;
    
    if (pro_init_complete && report_count <= 20) {
        should_log = true;  // Log first 20 reports after switching to mode 0x30
    }
    
    // For mode 0x30 (49+ bytes), check bytes 3-11 for changes
    if (len >= 49) {
        // Check if button/stick bytes changed
        static uint8_t last_b3 = 0, last_b4 = 0, last_b5 = 0;
        static uint8_t last_b6 = 0, last_b7 = 0, last_b8 = 0;
        
        if (report[3] != last_b3 || report[4] != last_b4 || report[5] != last_b5 ||
            report[6] != last_b6 || report[7] != last_b7 || report[8] != last_b8) {
            should_log = true;
            last_b3 = report[3]; last_b4 = report[4]; last_b5 = report[5];
            last_b6 = report[6]; last_b7 = report[7]; last_b8 = report[8];
            printf("*** MODE 0x30 VALUES CHANGED! ***\n");
        }
    } else if (len >= 7) {
        // Simple HID mode
        uint16_t current_btns = report[0] | (report[1] << 8);
        uint8_t current_x = report[3];
        uint8_t current_y = report[4];
        
        if (current_btns != last_report_btns || 
            current_x != last_report_x || 
            current_y != last_report_y) {
            should_log = true;
            last_report_btns = current_btns;
            last_report_x = current_x;
            last_report_y = current_y;
            printf("*** SIMPLE HID VALUES CHANGED! ***\n");
        }
    }
    
    if (should_log) {
        printf("Switch report #%lu (len=%d): ", report_count, len);
        for (int i = 0; i < (len < 16 ? len : 16); i++) {
            printf("%02X ", report[i]);
        }
        if (len >= 49) {
            printf("... [bytes 3-11]: ");
            for (int i = 3; i <= 11 && i < len; i++) {
                printf("%02X ", report[i]);
            }
        }
        printf("\n");
    }
#endif
    
    // Parse report based on length
    // Simple HID mode (7-8 bytes): Standard gamepad format
    // Full mode 0x30 (49+ bytes): Nintendo proprietary format after initialization
    
    if (len >= 49) {
        // Mode 0x30: Full input report format (after initialization)
        // Based on BetterJoy parsing logic
        
        // Buttons - Pro Controller uses bytes 3, 4, 5
        // Byte 3: Right buttons (Y=0x01, B=0x04, A=0x08, X=0x02, R=0x40, ZR=0x80)
        // Byte 4: Middle (Minus=0x01, Plus=0x02, L-stick=0x04, R-stick=0x08, Home=0x10, Capture=0x20)
        // Byte 5: Left buttons (Down=0x01, Up=0x02, Right=0x04, Left=0x08, L=0x40, ZL=0x80)
        
        uint8_t right_btns = report[3];
        uint8_t mid_btns = report[4];
        uint8_t left_btns = report[5];
        
        // Build button bitfield
        ctrl->buttons = 0;
        if (right_btns & 0x01) ctrl->buttons |= SWITCH_BTN_Y;
        if (right_btns & 0x02) ctrl->buttons |= SWITCH_BTN_X;
        if (right_btns & 0x04) ctrl->buttons |= SWITCH_BTN_B;
        if (right_btns & 0x08) ctrl->buttons |= SWITCH_BTN_A;
        if (right_btns & 0x40) ctrl->buttons |= SWITCH_BTN_R;
        if (right_btns & 0x80) ctrl->buttons |= SWITCH_BTN_ZR;
        if (left_btns & 0x40) ctrl->buttons |= SWITCH_BTN_L;
        if (left_btns & 0x80) ctrl->buttons |= SWITCH_BTN_ZL;
        if (mid_btns & 0x01) ctrl->buttons |= SWITCH_BTN_MINUS;
        if (mid_btns & 0x02) ctrl->buttons |= SWITCH_BTN_PLUS;
        if (mid_btns & 0x10) ctrl->buttons |= SWITCH_BTN_HOME;
        
        // D-Pad from left_btns byte
        ctrl->dpad = 8; // Center
        if ((left_btns & 0x01) && (left_btns & 0x04)) ctrl->dpad = SWITCH_DPAD_DOWN_RIGHT;
        else if ((left_btns & 0x01) && (left_btns & 0x08)) ctrl->dpad = SWITCH_DPAD_DOWN_LEFT;
        else if ((left_btns & 0x02) && (left_btns & 0x04)) ctrl->dpad = SWITCH_DPAD_UP_RIGHT;
        else if ((left_btns & 0x02) && (left_btns & 0x08)) ctrl->dpad = SWITCH_DPAD_UP_LEFT;
        else if (left_btns & 0x02) ctrl->dpad = SWITCH_DPAD_UP;
        else if (left_btns & 0x01) ctrl->dpad = SWITCH_DPAD_DOWN;
        else if (left_btns & 0x08) ctrl->dpad = SWITCH_DPAD_LEFT;
        else if (left_btns & 0x04) ctrl->dpad = SWITCH_DPAD_RIGHT;
        
        // Analog sticks - 12-bit precision packed into 3 bytes
        // Left stick: bytes 6-8
        uint8_t lx_raw[3] = {report[6], report[7], report[8]};
        uint16_t lx_12bit = lx_raw[0] | ((lx_raw[1] & 0x0F) << 8);
        uint16_t ly_12bit = (lx_raw[1] >> 4) | (lx_raw[2] << 4);
        
        // Right stick: bytes 9-11
        uint8_t rx_raw[3] = {report[9], report[10], report[11]};
        uint16_t rx_12bit = rx_raw[0] | ((rx_raw[1] & 0x0F) << 8);
        uint16_t ry_12bit = (rx_raw[1] >> 4) | (rx_raw[2] << 4);
        
        // Convert 12-bit (0-4095, center ~2048) to -128 to +127
        // Apply deadzone at 12-bit level to fix stick drift (±256 = ~6% deadzone)
        int32_t lx_delta = (int32_t)lx_12bit - 2048;
        int32_t ly_delta = 2048 - (int32_t)ly_12bit;  // Y inverted
        int32_t rx_delta = (int32_t)rx_12bit - 2048;
        int32_t ry_delta = 2048 - (int32_t)ry_12bit;  // Y inverted
        
        // Apply deadzone: if within ±256 of center, clamp to 0
        #define STICK_12BIT_DEADZONE 256
        if (lx_delta > -STICK_12BIT_DEADZONE && lx_delta < STICK_12BIT_DEADZONE) lx_delta = 0;
        if (ly_delta > -STICK_12BIT_DEADZONE && ly_delta < STICK_12BIT_DEADZONE) ly_delta = 0;
        if (rx_delta > -STICK_12BIT_DEADZONE && rx_delta < STICK_12BIT_DEADZONE) rx_delta = 0;
        if (ry_delta > -STICK_12BIT_DEADZONE && ry_delta < STICK_12BIT_DEADZONE) ry_delta = 0;
        
        ctrl->stick_left_x = lx_delta / 16;
        ctrl->stick_left_y = ly_delta / 16;
        ctrl->stick_right_x = rx_delta / 16;
        ctrl->stick_right_y = ry_delta / 16;
        
        // Update values for OLED display
        last_buttons = ctrl->buttons;
        last_dpad = ctrl->dpad;
        last_lx = ctrl->stick_left_x;
        last_ly = ctrl->stick_left_y;
        
#if ENABLE_SWITCH_DEBUG
        // Debug: Log parsed values when logging is active
        if (should_log) {
            printf("  Mode 0x30: Btns=0x%04X, DPad=%d, LX=%d, LY=%d, RX=%d, RY=%d\n",
                   ctrl->buttons, ctrl->dpad, ctrl->stick_left_x, ctrl->stick_left_y,
                   ctrl->stick_right_x, ctrl->stick_right_y);
        }
#endif
    } else if (len >= 7) {
        // Simple HID mode (before initialization): Standard gamepad format
        // Byte 0-1: Buttons (16-bit)
        // Byte 2: D-Pad (hat switch) 
        // Byte 3-6: Sticks (8-bit each)
        
        ctrl->buttons = report[0] | (report[1] << 8);
        ctrl->dpad = report[2];
        
        // Parse analog sticks (convert 0-255 range to -128 to 127)
        ctrl->stick_left_x = (int16_t)report[3] - 128;
        ctrl->stick_left_y = 128 - (int16_t)report[4];  // Y inverted
        ctrl->stick_right_x = (int16_t)report[5] - 128;
        ctrl->stick_right_y = 128 - (int16_t)report[6];  // Y inverted
        
        // Update values for OLED display
        last_buttons = ctrl->buttons;
        last_dpad = ctrl->dpad;
        last_lx = ctrl->stick_left_x;
        last_ly = ctrl->stick_left_y;
        
#if ENABLE_SWITCH_DEBUG
        // Debug: Log parsed values when logging is active
        if (should_log) {
            printf("  Simple HID: Btns=0x%04X, DPad=%d, LX=%d, LY=%d, RX=%d, RY=%d\n",
                   ctrl->buttons, ctrl->dpad, ctrl->stick_left_x, ctrl->stick_left_y,
                   ctrl->stick_right_x, ctrl->stick_right_y);
        }
#endif
    }
}

void switch_to_atari(const switch_controller_t* sw, uint8_t joystick_num, 
                     uint8_t* direction, uint8_t* fire) {
    if (!sw || !direction || !fire) {
        return;
    }
    
    *direction = 0;
    *fire = 0;
    
    // Debug: Log conversion periodically
    static uint32_t convert_count = 0;
    convert_count++;
    
    // D-Pad has priority (arcade stick likely uses D-Pad)
    switch (sw->dpad) {
        case SWITCH_DPAD_UP:
            *direction |= 0x01;
            break;
        case SWITCH_DPAD_UP_RIGHT:
            *direction |= 0x01 | 0x08;
            break;
        case SWITCH_DPAD_RIGHT:
            *direction |= 0x08;
            break;
        case SWITCH_DPAD_DOWN_RIGHT:
            *direction |= 0x02 | 0x08;
            break;
        case SWITCH_DPAD_DOWN:
            *direction |= 0x02;
            break;
        case SWITCH_DPAD_DOWN_LEFT:
            *direction |= 0x02 | 0x04;
            break;
        case SWITCH_DPAD_LEFT:
            *direction |= 0x04;
            break;
        case SWITCH_DPAD_UP_LEFT:
            *direction |= 0x01 | 0x04;
            break;
    }
    
    // If D-Pad not used, check left analog stick (for Pro Controller)
    if (*direction == 0) {
        if (abs(sw->stick_left_x) > sw->deadzone || abs(sw->stick_left_y) > sw->deadzone) {
            // Left stick
            if (sw->stick_left_x < -sw->deadzone)  *direction |= 0x04;  // Left
            if (sw->stick_left_x > sw->deadzone)   *direction |= 0x08;  // Right
            
            // Y axis
            if (sw->stick_left_y < -sw->deadzone)  *direction |= 0x01;  // Up
            if (sw->stick_left_y > sw->deadzone)   *direction |= 0x02;  // Down
        }
    }
    
    // Fire button mapping
    // B button = primary (Nintendo B is like Xbox A position)
    // A button = secondary
    // ZR trigger = alternative
    if (sw->buttons & SWITCH_BTN_B) {
        *fire = 1;
    } else if (sw->buttons & SWITCH_BTN_A) {
        *fire = 1;
    } else if (sw->buttons & SWITCH_BTN_ZR) {
        *fire = 1;
    }
    
    // Update Atari output for OLED display
    last_atari_dir = *direction;
    last_atari_fire = *fire;
    
#if ENABLE_SWITCH_DEBUG
    // Debug: Log every 100th conversion to see what's happening
    if (convert_count % 100 == 1) {
        printf("Switch->Atari #%lu: Btns=0x%04X DPad=%d LX=%d LY=%d => Dir=0x%02X Fire=%d\n",
               convert_count, sw->buttons, sw->dpad, sw->stick_left_x, sw->stick_left_y,
               *direction, *fire);
    }
#endif
}

// USB handshake command structure (2 bytes sent as feature report)
static bool send_usb_command(uint8_t dev_addr, uint8_t cmd) {
    uint8_t buf[2] = {0x80, cmd};
    
#if ENABLE_SWITCH_DEBUG
    printf("Switch: Sending USB command 0x80 0x%02X...", cmd);
#endif
    
    // Pump USB stack before sending to ensure previous transfer is complete
    for (int i = 0; i < 10; i++) {
        tuh_task();
        sleep_ms(1);
    }
    
    // Send as an output report to endpoint 0
    bool result = tuh_hid_send_report(dev_addr, 0, 0, buf, 2);
    
#if ENABLE_SWITCH_DEBUG
    printf(" result=%d\n", result);
#endif
    
    // Wait longer for controller to process and respond
    // Pump USB stack while waiting
    for (int i = 0; i < 150; i++) {
        tuh_task();
        sleep_ms(1);
    }
    return result;
}

// Send a subcommand to the Pro Controller
// Format: [0x01][counter][rumble_data 8 bytes][subcommand][data...]
static bool send_subcommand(uint8_t dev_addr, uint8_t subcmd, const uint8_t* data, uint8_t data_len) {
    uint8_t buf[64] = {0};
    
    buf[0] = 0x01;           // Output report ID
    buf[1] = global_count;   // Packet counter (0x00-0x0F)
    
    // Rumble data (8 bytes) - use neutral rumble
    buf[2] = 0x00; buf[3] = 0x01; buf[4] = 0x40; buf[5] = 0x40;  // Left rumble
    buf[6] = 0x00; buf[7] = 0x01; buf[8] = 0x40; buf[9] = 0x40;  // Right rumble
    
    buf[10] = subcmd;        // Subcommand ID
    
    // Copy subcommand data
    if (data && data_len > 0) {
        memcpy(&buf[11], data, data_len);
    }
    
#if ENABLE_SWITCH_DEBUG
    printf("Switch: Sending subcommand 0x%02X (counter=%d, data_len=%d)...", subcmd, global_count, data_len);
#endif
    
    // Increment counter (wraps at 0x0F)
    global_count = (global_count + 1) & 0x0F;
    
    // Pump USB stack before sending to ensure previous transfer is complete
    for (int i = 0; i < 10; i++) {
        tuh_task();
        sleep_ms(1);
    }
    
    // Send as output report
    bool result = tuh_hid_send_report(dev_addr, 0, 0, buf, 11 + data_len);
    
#if ENABLE_SWITCH_DEBUG
    printf(" result=%d\n", result);
#endif
    
    // Wait for controller to respond, pumping USB stack
    for (int i = 0; i < 200; i++) {
        tuh_task();
        sleep_ms(1);
    }
    return result;
}

bool switch_init_pro_controller(uint8_t dev_addr) {
#if ENABLE_SWITCH_DEBUG
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Switch Pro Controller USB Initialization (BetterJoy style)\n");
    printf("═══════════════════════════════════════════════════════\n");
#endif
    
    global_count = 0;
    init_cmd_success = 0;  // Reset bitmask
    
    // Step 1: USB Handshaking sequence (BetterJoy EXACT sequence)
#if ENABLE_SWITCH_DEBUG
    printf("Switch: Step 1 - USB Handshake 0x02\n");
#endif
    if (send_usb_command(dev_addr, 0x02)) init_cmd_success |= (1 << 0);
    
#if ENABLE_SWITCH_DEBUG
    printf("Switch: Step 2 - Set 3Mbit baud rate 0x03\n");
#endif
    if (send_usb_command(dev_addr, 0x03)) init_cmd_success |= (1 << 1);
    
#if ENABLE_SWITCH_DEBUG
    printf("Switch: Step 3 - Handshake at new baud rate 0x02\n");
#endif
    if (send_usb_command(dev_addr, 0x02)) init_cmd_success |= (1 << 2);
    
#if ENABLE_SWITCH_DEBUG
    printf("Switch: Step 4 - Prevent HID timeout 0x04\n");
#endif
    if (send_usb_command(dev_addr, 0x04)) init_cmd_success |= (1 << 3);
    
    sleep_ms(100);
    
    // Step 2: Enable IMU (subcommand 0x40)
#if ENABLE_SWITCH_DEBUG
    printf("Switch: Step 5 - Enable IMU (subcommand 0x40)\n");
#endif
    uint8_t imu_enable = 0x01;
    if (send_subcommand(dev_addr, 0x40, &imu_enable, 1)) init_cmd_success |= (1 << 4);
    
    // Step 3: Enable vibration (subcommand 0x48)
#if ENABLE_SWITCH_DEBUG
    printf("Switch: Step 6 - Enable vibration (subcommand 0x48)\n");
#endif
    uint8_t vib_enable = 0x01;
    if (send_subcommand(dev_addr, 0x48, &vib_enable, 1)) init_cmd_success |= (1 << 5);
    
    // Step 4: Set input report mode to 0x30 (FULL MODE - this is the critical one!)
#if ENABLE_SWITCH_DEBUG
    printf("Switch: Step 7 - Set input mode 0x30 (FULL MODE)\n");
#endif
    uint8_t input_mode = 0x30;
    if (send_subcommand(dev_addr, 0x03, &input_mode, 1)) init_cmd_success |= (1 << 6);
    
    // Keep this message - it's useful for users to know initialization completed
    printf("Switch Pro Controller initialized (cmds: 0x%02X/0x7F)\n", init_cmd_success);
    
    return true;
}

void switch_mount_cb(uint8_t dev_addr) {
    // Get VID/PID to identify specific controller
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    
    const char* controller_name = "Unknown";
    if (vid == SWITCH_VENDOR_ID) {
        if (pid == SWITCH_PRO_CONTROLLER) controller_name = "Pro Controller";
        else if (pid == SWITCH_JOYCON_L) controller_name = "Joy-Con Left";
        else if (pid == SWITCH_JOYCON_R) controller_name = "Joy-Con Right";
        else if (pid == SWITCH_JOYCON_PAIR) controller_name = "Joy-Con Pair";
    } else if (vid == POWERA_VENDOR_ID) {
        if (pid == POWERA_FUSION_ARCADE) controller_name = "PowerA Fusion Arcade";
        else controller_name = "PowerA Controller";
    }
    printf("Switch controller mount: %s (addr=%d, VID=0x%04X, PID=0x%04X)\n", 
           controller_name, dev_addr, vid, pid);
    
    // Show on OLED (matching PS4/Xbox splash screen style)
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 10, 10, 2, (char*)"SWITCH!");
    
    // Show controller type
    if (vid == POWERA_VENDOR_ID && pid == POWERA_FUSION_ARCADE) {
        ssd1306_draw_string(&disp, 5, 35, 1, (char*)"PowerA Arcade");
    } else if (vid == SWITCH_VENDOR_ID && pid == SWITCH_PRO_CONTROLLER) {
        ssd1306_draw_string(&disp, 10, 35, 1, (char*)"Pro Controller");
    } else {
        ssd1306_draw_string(&disp, 15, 35, 1, (char*)"Controller");
    }
    
    // Show debug info: Address (matching PS4/Xbox format)
    char debug_line[20];
    snprintf(debug_line, sizeof(debug_line), "Addr:%d", dev_addr);
    ssd1306_draw_string(&disp, 25, 50, 1, debug_line);
    
    ssd1306_show(&disp);
    sleep_ms(2000);  // Match PS4 timing for consistency
    
    // Allocate controller first
    switch_controller_t* ctrl = allocate_controller(dev_addr);
    if (ctrl) {
        // Schedule delayed initialization for Pro Controller
        if (vid == SWITCH_VENDOR_ID && pid == SWITCH_PRO_CONTROLLER) {
            pro_needs_init = true;
            pro_dev_addr = dev_addr;
            pro_mount_time = to_ms_since_boot(get_absolute_time());
            pro_init_attempted = false;
            pro_init_complete = false;
            global_count = 0;
        }
    } else {
        printf("Switch: ERROR - Failed to allocate controller!\n");
    }
}

void switch_unmount_cb(uint8_t dev_addr) {
    printf("Switch controller unmount (addr=%d)\n", dev_addr);
    
    // Clear delayed init state if this was the Pro Controller
    if (dev_addr == pro_dev_addr) {
        pro_needs_init = false;
        pro_init_attempted = false;
    }
    
    free_controller(dev_addr);
}

void switch_set_deadzone(uint8_t dev_addr, int16_t deadzone) {
    switch_controller_t* ctrl = switch_get_controller(dev_addr);
    if (ctrl) {
        ctrl->deadzone = deadzone;
    }
}

