/*
 * Atari ST RP2040 IKBD Emulator - Google Stadia Controller Support
 * Copyright (C) 2025
 * 
 * Google Stadia controller implementation
 */

#include "stadia_controller.h"
#include "tusb.h"
#include "ssd1306.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ssd1306_t disp;

// Controller storage
static stadia_controller_t controllers[MAX_STADIA_CONTROLLERS];

// Allocate a controller slot
static stadia_controller_t* allocate_controller(uint8_t dev_addr) {
    for (int i = 0; i < MAX_STADIA_CONTROLLERS; i++) {
        if (!controllers[i].connected) {
            memset(&controllers[i], 0, sizeof(stadia_controller_t));
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
    for (int i = 0; i < MAX_STADIA_CONTROLLERS; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected) {
            memset(&controllers[i], 0, sizeof(stadia_controller_t));
            return;
        }
    }
}

bool stadia_is_controller(uint16_t vid, uint16_t pid) {
    // Google Stadia controller
    if (vid == STADIA_VENDOR_ID && pid == STADIA_CONTROLLER) {
        return true;
    }
    return false;
}

stadia_controller_t* stadia_get_controller(uint8_t dev_addr) {
    for (int i = 0; i < MAX_STADIA_CONTROLLERS; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected) {
            return &controllers[i];
        }
    }
    return NULL;
}

void stadia_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len) {
    if (!report || len == 0) return;
    
    stadia_controller_t* ctrl = stadia_get_controller(dev_addr);
    if (!ctrl) return;
    
    // Stadia controller uses standard HID gamepad format
    // Typical report structure (may vary):
    // Byte 0-1: Buttons (16-bit)
    // Byte 2: D-Pad (hat switch)
    // Byte 3-4: Left stick X (8-bit or 16-bit)
    // Byte 5-6: Left stick Y
    // Byte 7-8: Right stick X
    // Byte 9-10: Right stick Y
    // Byte 11: Left trigger
    // Byte 12: Right trigger
    
    if (len >= 8) {
        // Parse buttons
        ctrl->buttons = report[0] | (report[1] << 8);
        
        // Parse D-Pad (hat switch)
        ctrl->dpad = (len > 2) ? report[2] : 15;
        
        // Parse analog sticks (convert 0-255 range to -128 to 127)
        if (len >= 7) {
            ctrl->stick_left_x = (int16_t)report[3] - 128;
            ctrl->stick_left_y = 128 - (int16_t)report[4];  // Y inverted
            ctrl->stick_right_x = (int16_t)report[5] - 128;
            ctrl->stick_right_y = 128 - (int16_t)report[6];  // Y inverted
        }
        
        // Parse triggers
        if (len >= 9) {
            ctrl->trigger_left = report[7];
            ctrl->trigger_right = report[8];
        }
    }
}

void stadia_to_atari(const stadia_controller_t* stadia, uint8_t joystick_num, 
                     uint8_t* direction, uint8_t* fire) {
    if (!stadia || !direction || !fire) {
        return;
    }
    
    *direction = 0;
    *fire = 0;
    
    // D-Pad has priority
    switch (stadia->dpad) {
        case STADIA_DPAD_UP:
            *direction |= 0x01;
            break;
        case STADIA_DPAD_UP_RIGHT:
            *direction |= 0x01 | 0x08;
            break;
        case STADIA_DPAD_RIGHT:
            *direction |= 0x08;
            break;
        case STADIA_DPAD_DOWN_RIGHT:
            *direction |= 0x02 | 0x08;
            break;
        case STADIA_DPAD_DOWN:
            *direction |= 0x02;
            break;
        case STADIA_DPAD_DOWN_LEFT:
            *direction |= 0x02 | 0x04;
            break;
        case STADIA_DPAD_LEFT:
            *direction |= 0x04;
            break;
        case STADIA_DPAD_UP_LEFT:
            *direction |= 0x01 | 0x04;
            break;
    }
    
    // If D-Pad not used, check left analog stick
    if (*direction == 0) {
        if (abs(stadia->stick_left_x) > stadia->deadzone || abs(stadia->stick_left_y) > stadia->deadzone) {
            // Left stick
            if (stadia->stick_left_x < -stadia->deadzone)  *direction |= 0x04;  // Left
            if (stadia->stick_left_x > stadia->deadzone)   *direction |= 0x08;  // Right
            
            // Y axis
            if (stadia->stick_left_y < -stadia->deadzone)  *direction |= 0x01;  // Up
            if (stadia->stick_left_y > stadia->deadzone)   *direction |= 0x02;  // Down
        }
    }
    
    // Fire button mapping
    // A button = primary (bottom button on right side)
    // B button = secondary (right button)
    // X button = alternative (left button)
    // Y button = alternative (top button)
    // Triggers also work as fire
    if (stadia->buttons & STADIA_BTN_A) {
        *fire = 1;
    } else if (stadia->buttons & STADIA_BTN_B) {
        *fire = 1;
    } else if (stadia->buttons & STADIA_BTN_X) {
        *fire = 1;
    } else if (stadia->buttons & STADIA_BTN_Y) {
        *fire = 1;
    } else if (stadia->buttons & STADIA_BTN_R1) {
        *fire = 1;
    } else if (stadia->buttons & STADIA_BTN_R2) {
        *fire = 1;
    }
}

void stadia_mount_cb(uint8_t dev_addr) {
    printf("Stadia controller mount (addr=%d)\n", dev_addr);
    
    // Show on OLED (matching other controller splash screen style)
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 10, 10, 2, (char*)"STADIA!");
    
    // Show controller type
    ssd1306_draw_string(&disp, 5, 35, 1, (char*)"Google Controller");
    
    // Show debug info: Address
    char debug_line[20];
    snprintf(debug_line, sizeof(debug_line), "Addr:%d", dev_addr);
    ssd1306_draw_string(&disp, 25, 50, 1, debug_line);
    
    ssd1306_show(&disp);
    sleep_ms(2000);
    
    // Allocate controller
    stadia_controller_t* ctrl = allocate_controller(dev_addr);
    if (!ctrl) {
        printf("Stadia: ERROR - Failed to allocate controller!\n");
    }
}

void stadia_unmount_cb(uint8_t dev_addr) {
    printf("Stadia controller unmount (addr=%d)\n", dev_addr);
    free_controller(dev_addr);
}

void stadia_set_deadzone(uint8_t dev_addr, int16_t deadzone) {
    stadia_controller_t* ctrl = stadia_get_controller(dev_addr);
    if (ctrl) {
        ctrl->deadzone = deadzone;
    }
}

