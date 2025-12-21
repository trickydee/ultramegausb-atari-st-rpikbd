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

#if ENABLE_OLED_DISPLAY
extern ssd1306_t disp;
#endif

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
    
    static bool first_report = true;
    static uint32_t report_count = 0;
    report_count++;
    
    if (first_report || (report_count % 50) == 0) {
        extern ssd1306_t disp;
        ssd1306_clear(&disp);
        
        char line[22];
        // Header: "v8.0.1 L:10"
        snprintf(line, sizeof(line), "v8.0.1 Len:%d", len);
        ssd1306_draw_string(&disp, 0, 0, 1, line);
        
        // Show bytes 0-3
        char hex1[20];
        if (len >= 4) {
            snprintf(hex1, sizeof(hex1), "%02X %02X %02X %02X", 
                     report[0], report[1], report[2], report[3]);
            ssd1306_draw_string(&disp, 0, 10, 1, hex1);
        }
        
        // Show bytes 4-7
        char hex2[20];
        if (len >= 8) {
            snprintf(hex2, sizeof(hex2), "%02X %02X %02X %02X", 
                     report[4], report[5], report[6], report[7]);
            ssd1306_draw_string(&disp, 0, 20, 1, hex2);
        }
        
        // Show bytes 8-11 (or whatever remains)
        char hex3[20];
        if (len > 8) {
            // Show up to 4 bytes starting at byte 8
            int show_bytes = (len - 8) > 4 ? 4 : (len - 8);
            if (show_bytes == 4) {
                snprintf(hex3, sizeof(hex3), "%02X %02X %02X %02X", 
                         report[8], report[9], report[10], report[11]);
            } else if (show_bytes == 3) {
                snprintf(hex3, sizeof(hex3), "%02X %02X %02X", 
                         report[8], report[9], report[10]);
            } else if (show_bytes == 2) {
                snprintf(hex3, sizeof(hex3), "%02X %02X", 
                         report[8], report[9]);
            } else if (show_bytes == 1) {
                snprintf(hex3, sizeof(hex3), "%02X", 
                         report[8]);
            }
            ssd1306_draw_string(&disp, 0, 30, 1, hex3);
        }
        
        ssd1306_show(&disp);
        first_report = false;
    }
    
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

static void stadia_compute_axes(const stadia_controller_t* stadia,
                                uint8_t* left_axis, uint8_t* fire,
                                uint8_t* right_axis, uint8_t* joy0_fire) {
    if (left_axis) {
        *left_axis = 0;
        
        switch (stadia->dpad) {
            case STADIA_DPAD_UP:         *left_axis |= 0x01; break;
            case STADIA_DPAD_UP_RIGHT:   *left_axis |= 0x01 | 0x08; break;
            case STADIA_DPAD_RIGHT:      *left_axis |= 0x08; break;
            case STADIA_DPAD_DOWN_RIGHT: *left_axis |= 0x02 | 0x08; break;
            case STADIA_DPAD_DOWN:       *left_axis |= 0x02; break;
            case STADIA_DPAD_DOWN_LEFT:  *left_axis |= 0x02 | 0x04; break;
            case STADIA_DPAD_LEFT:       *left_axis |= 0x04; break;
            case STADIA_DPAD_UP_LEFT:    *left_axis |= 0x01 | 0x04; break;
        }
        
        if (*left_axis == 0) {
            if (abs(stadia->stick_left_x) > stadia->deadzone || abs(stadia->stick_left_y) > stadia->deadzone) {
                if (stadia->stick_left_x < -stadia->deadzone)  *left_axis |= 0x04;
                if (stadia->stick_left_x > stadia->deadzone)   *left_axis |= 0x08;
                if (stadia->stick_left_y < -stadia->deadzone)  *left_axis |= 0x01;
                if (stadia->stick_left_y > stadia->deadzone)   *left_axis |= 0x02;
            }
        }
    }
    
    if (fire) {
        *fire = ((stadia->buttons & (STADIA_BTN_A | STADIA_BTN_B |
                                     STADIA_BTN_X | STADIA_BTN_Y |
                                     STADIA_BTN_R1 | STADIA_BTN_R2))) ? 1 : 0;
    }
    
    if (right_axis) {
        *right_axis = 0;
        if (abs(stadia->stick_right_x) > stadia->deadzone || abs(stadia->stick_right_y) > stadia->deadzone) {
            if (stadia->stick_right_x < -stadia->deadzone)  *right_axis |= 0x04;
            if (stadia->stick_right_x > stadia->deadzone)   *right_axis |= 0x08;
            if (stadia->stick_right_y < -stadia->deadzone)  *right_axis |= 0x01;
            if (stadia->stick_right_y > stadia->deadzone)   *right_axis |= 0x02;
        }
    }
    
    if (joy0_fire) {
        *joy0_fire = (stadia->buttons & STADIA_BTN_B) ? 1 : 0;
    }
}

void stadia_to_atari(const stadia_controller_t* stadia, uint8_t joystick_num, 
                     uint8_t* direction, uint8_t* fire) {
    if (!stadia || !direction || !fire) {
        return;
    }
    uint8_t right_dummy;
    stadia_compute_axes(stadia, direction, fire, &right_dummy, NULL);
}

void stadia_mount_cb(uint8_t dev_addr) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  🎮 GOOGLE STADIA CONTROLLER DETECTED!\n");
    printf("  Device Address: %d\n", dev_addr);
    printf("  \n");
    printf("  Button mapping:\n");
    printf("  - Left Stick / D-Pad = Directions\n");
    printf("  - A/B/X/Y Buttons = Fire\n");
    printf("  - L/R Triggers = Fire\n");
    printf("  \n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("\n");
    
#if ENABLE_OLED_DISPLAY
    // Show on OLED (matching PS4/Xbox/Switch style)
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 15, 10, 2, (char*)"STADIA");
    ssd1306_draw_string(&disp, 5, 35, 1, (char*)"Google Controller");
    
    // Show debug info: Address
    char debug_line[20];
    snprintf(debug_line, sizeof(debug_line), "Addr:%d", dev_addr);
    ssd1306_draw_string(&disp, 35, 50, 1, debug_line);
    
    ssd1306_show(&disp);
    sleep_ms(2000);
#endif
    
    // Allocate controller
    stadia_controller_t* ctrl = allocate_controller(dev_addr);
    if (ctrl) {
        printf("Stadia: Controller registered and ready!\n");
    } else {
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

uint8_t stadia_connected_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_STADIA_CONTROLLERS; i++) {
        if (controllers[i].connected) {
            count++;
        }
    }
    return count;
}

bool stadia_llamatron_axes(uint8_t* joy1_axis, uint8_t* joy1_fire,
                           uint8_t* joy0_axis, uint8_t* joy0_fire) {
    for (uint8_t i = 0; i < MAX_STADIA_CONTROLLERS; i++) {
        if (controllers[i].connected) {
            stadia_compute_axes(&controllers[i], joy1_axis, joy1_fire, joy0_axis, joy0_fire);
            return true;
        }
    }
    return false;
}

