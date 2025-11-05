/*
 * Atari ST RP2040 IKBD Emulator - PS4 DualShock 4 Support
 * Copyright (C) 2025
 * 
 * PS4 DualShock 4 controller implementation
 * Based on TinyUSB HID controller example
 */

#include "ps4_controller.h"
#include "tusb.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>

//--------------------------------------------------------------------
// Controller Storage
//--------------------------------------------------------------------

#define MAX_PS4_CONTROLLERS  2

static ps4_controller_t controllers[MAX_PS4_CONTROLLERS];
static uint8_t controller_count = 0;

//--------------------------------------------------------------------
// Helper Functions
//--------------------------------------------------------------------

static ps4_controller_t* find_controller_by_addr(uint8_t dev_addr) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected) {
            return &controllers[i];
        }
    }
    return NULL;
}

static ps4_controller_t* allocate_controller(uint8_t dev_addr) {
    if (controller_count >= MAX_PS4_CONTROLLERS) {
        printf("PS4: Max controllers reached\n");
        return NULL;
    }
    
    ps4_controller_t* ctrl = &controllers[controller_count++];
    memset(ctrl, 0, sizeof(ps4_controller_t));
    ctrl->dev_addr = dev_addr;
    ctrl->connected = true;
    ctrl->deadzone = 50;  // Increased deadzone for drift compensation (~39% of range)
    
    return ctrl;
}

static void free_controller(uint8_t dev_addr) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].dev_addr == dev_addr) {
            for (uint8_t j = i; j < controller_count - 1; j++) {
                controllers[j] = controllers[j + 1];
            }
            controller_count--;
            break;
        }
    }
}

//--------------------------------------------------------------------
// Public API Implementation
//--------------------------------------------------------------------

bool ps4_is_dualshock4(uint16_t vid, uint16_t pid) {
    if (vid != PS4_VENDOR_ID) {
        return false;
    }
    
    switch (pid) {
        case PS4_DS4_PID_V1:
        case PS4_DS4_PID_V2:
        case PS4_DS4_PID_DONGLE:
            return true;
        default:
            return false;
    }
}

bool ps4_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len) {
    static bool first_report_ever = true;
#if ENABLE_OLED_DISPLAY
    extern ssd1306_t disp;
#endif
    
    ps4_controller_t* ctrl = find_controller_by_addr(dev_addr);
    if (!ctrl) {
        printf("PS4: Controller %d not found\n", dev_addr);
        return false;
    }
    
    // PS4 reports are at least 9 bytes
    if (len < 9) {
        printf("PS4: Report too short (%d bytes)\n", len);
        return false;
    }
    
    // Show first report (minimal, no blocking)
    if (first_report_ever) {
        first_report_ever = false;
        printf("PS4: First report received (%d bytes)\n", len);
        // No OLED update or sleep - keep it fast!
    }
    
    // Parse PS4 report
    // Note: USB PS4 reports typically start at byte 0, but may have report ID
    // Let's check first byte to see if it's a report ID or data
    ps4_report_t* input = &ctrl->report;
    
    uint8_t offset = 0;
    
    // If first byte looks like report ID (0x01, 0x11, etc), skip it
    if (report[0] == 0x01 || report[0] == 0x11) {
        offset = 1;  // Skip report ID
        printf("PS4: Report has ID byte: 0x%02X\n", report[0]);
    }
    
    input->x = report[offset + 0];
    input->y = report[offset + 1];
    input->z = report[offset + 2];        // Right stick X
    input->rz = report[offset + 3];       // Right stick Y
    
    // Buttons in byte 4
    uint8_t buttons1 = report[offset + 4];
    input->dpad = buttons1 & 0x0F;
    input->square = (buttons1 >> 4) & 1;
    input->cross = (buttons1 >> 5) & 1;
    input->circle = (buttons1 >> 6) & 1;
    input->triangle = (buttons1 >> 7) & 1;
    
    // Buttons in byte 5
    uint8_t buttons2 = report[offset + 5];
    input->l1 = buttons2 & 1;
    input->r1 = (buttons2 >> 1) & 1;
    input->l2 = (buttons2 >> 2) & 1;
    input->r2 = (buttons2 >> 3) & 1;
    input->share = (buttons2 >> 4) & 1;
    input->options = (buttons2 >> 5) & 1;
    input->l3 = (buttons2 >> 6) & 1;
    input->r3 = (buttons2 >> 7) & 1;
    
    // PS button and touchpad in byte 6
    if (len > offset + 6) {
        uint8_t buttons3 = report[offset + 6];
        input->ps = buttons3 & 1;
        input->tpad = (buttons3 >> 1) & 1;
        input->counter = (buttons3 >> 2) & 0x3F;
    }
    
    // Analog triggers in bytes 7-8
    if (len > offset + 8) {
        input->l2_trigger = report[offset + 7];
        input->r2_trigger = report[offset + 8];
    }
    
    // Minimal debug output (every 500th report = ~2 seconds)
    static uint32_t report_count = 0;
    report_count++;
    
    #if 0  // Disable debug output for performance - enable only for troubleshooting
    if ((report_count % 500) == 0) {
        int8_t drift_x = input->x - 128;
        int8_t drift_y = input->y - 128;
        printf("PS4: Rpt %lu - Drift(%+d,%+d)\n", report_count, drift_x, drift_y);
    }
    #endif
    
    return true;
}

ps4_controller_t* ps4_get_controller(uint8_t dev_addr) {
    return find_controller_by_addr(dev_addr);
}

void ps4_to_atari(const ps4_controller_t* ps4, uint8_t joystick_num,
                  uint8_t* direction, uint8_t* fire) {
    if (!ps4 || !direction || !fire) {
        return;
    }
    
    const ps4_report_t* input = &ps4->report;
    *direction = 0;
    *fire = 0;
    
    // Convert analog stick to directions (left stick)
    // PS4 sticks are 0-255 with 128 as center
    int8_t stick_x = (int8_t)(input->x - 128);
    int8_t stick_y = (int8_t)(input->y - 128);
    
    // Apply deadzone
    if (stick_x < -ps4->deadzone || stick_x > ps4->deadzone ||
        stick_y < -ps4->deadzone || stick_y > ps4->deadzone) {
        
        if (stick_y < -ps4->deadzone) *direction |= 0x01;  // Up
        if (stick_y > ps4->deadzone)  *direction |= 0x02;  // Down
        if (stick_x < -ps4->deadzone) *direction |= 0x04;  // Left
        if (stick_x > ps4->deadzone)  *direction |= 0x08;  // Right
    }
    
    // D-Pad takes priority (if not centered)
    if (input->dpad != PS4_DPAD_CENTER) {
        switch (input->dpad) {
            case PS4_DPAD_UP:         *direction = 0x01; break;
            case PS4_DPAD_UP_RIGHT:   *direction = 0x09; break;  // Up + Right
            case PS4_DPAD_RIGHT:      *direction = 0x08; break;
            case PS4_DPAD_DOWN_RIGHT: *direction = 0x0A; break;  // Down + Right
            case PS4_DPAD_DOWN:       *direction = 0x02; break;
            case PS4_DPAD_DOWN_LEFT:  *direction = 0x06; break;  // Down + Left
            case PS4_DPAD_LEFT:       *direction = 0x04; break;
            case PS4_DPAD_UP_LEFT:    *direction = 0x05; break;  // Up + Left
        }
    }
    
    // Fire button mapping
    // Primary: Cross (X) button (most common in games)
    // Alternative: R2 trigger (if pressed > 50%)
    if (input->cross) {
        *fire = 1;
    } else if (input->r2_trigger > 128) {
        *fire = 1;
    }
    
    // Could also use Circle, Square, or R1 as fire
}

void ps4_set_deadzone(uint8_t dev_addr, int16_t deadzone) {
    ps4_controller_t* ctrl = find_controller_by_addr(dev_addr);
    if (ctrl) {
        ctrl->deadzone = deadzone;
        printf("PS4: Deadzone set to %d for controller %d\n", deadzone, dev_addr);
    }
}

void ps4_mount_cb(uint8_t dev_addr) {
#if ENABLE_OLED_DISPLAY
    extern ssd1306_t disp;
#endif
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  🎮 PS4 DUALSHOCK 4 DETECTED!\n");
    printf("  Device Address: %d\n", dev_addr);
    printf("  \n");
    printf("  PS4 controllers are standard HID devices\n");
    printf("  Should work immediately with TinyUSB 0.19.0!\n");
    printf("  \n");
    printf("  Button mapping:\n");
    printf("  - Left Stick / D-Pad = Directions\n");
    printf("  - Cross (X) = Fire\n");
    printf("  - R2 Trigger = Fire (alternative)\n");
    printf("  \n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("\n");
    
#if ENABLE_OLED_DISPLAY
    // Show on OLED
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 25, 10, 2, (char*)"PS4");
    ssd1306_draw_string(&disp, 10, 35, 1, (char*)"DualShock 4");
    
    // Show debug info: Address
    char debug_line[20];
    snprintf(debug_line, sizeof(debug_line), "Addr:%d", dev_addr);
    ssd1306_draw_string(&disp, 25, 50, 1, debug_line);
    
    ssd1306_show(&disp);
    sleep_ms(2000);
#endif  // Extended to match Xbox timing
    
    ps4_controller_t* ctrl = allocate_controller(dev_addr);
    if (ctrl) {
        printf("PS4: Controller registered and ready!\n");
    }
}

void ps4_unmount_cb(uint8_t dev_addr) {
    printf("PS4: Controller unmounted at address %d\n", dev_addr);
    free_controller(dev_addr);
}

