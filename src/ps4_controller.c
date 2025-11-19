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

static void ps4_compute_axes(const ps4_controller_t* ps4,
                             uint8_t* left_axis, uint8_t* fire,
                             uint8_t* right_axis, uint8_t* joy0_fire) {
    const ps4_report_t* input = &ps4->report;
    
    if (left_axis) {
        *left_axis = 0;
        
        int8_t stick_x = (int8_t)(input->x - 128);
        int8_t stick_y = (int8_t)(input->y - 128);
        
        if (stick_x < -ps4->deadzone || stick_x > ps4->deadzone ||
            stick_y < -ps4->deadzone || stick_y > ps4->deadzone) {
            
            if (stick_y < -ps4->deadzone) *left_axis |= 0x01;
            if (stick_y > ps4->deadzone)  *left_axis |= 0x02;
            if (stick_x < -ps4->deadzone) *left_axis |= 0x04;
            if (stick_x > ps4->deadzone)  *left_axis |= 0x08;
        }
        
        if (input->dpad != PS4_DPAD_CENTER) {
            switch (input->dpad) {
                case PS4_DPAD_UP:         *left_axis = 0x01; break;
                case PS4_DPAD_UP_RIGHT:   *left_axis = 0x09; break;
                case PS4_DPAD_RIGHT:      *left_axis = 0x08; break;
                case PS4_DPAD_DOWN_RIGHT: *left_axis = 0x0A; break;
                case PS4_DPAD_DOWN:       *left_axis = 0x02; break;
                case PS4_DPAD_DOWN_LEFT:  *left_axis = 0x06; break;
                case PS4_DPAD_LEFT:       *left_axis = 0x04; break;
                case PS4_DPAD_UP_LEFT:    *left_axis = 0x05; break;
            }
        }
    }
    
    if (fire) {
        *fire = (input->cross || input->r2_trigger > 128) ? 1 : 0;
    }
    
    if (right_axis) {
        *right_axis = 0;
        int8_t stick_rx = (int8_t)(input->z - 128);
        int8_t stick_ry = (int8_t)(input->rz - 128);
        
        if (stick_ry < -ps4->deadzone) *right_axis |= 0x01;
        if (stick_ry > ps4->deadzone)  *right_axis |= 0x02;
        if (stick_rx < -ps4->deadzone) *right_axis |= 0x04;
        if (stick_rx > ps4->deadzone)  *right_axis |= 0x08;
    }
    
    if (joy0_fire) {
        *joy0_fire = input->circle ? 1 : 0;
    }
}

void ps4_to_atari(const ps4_controller_t* ps4, uint8_t joystick_num,
                  uint8_t* direction, uint8_t* fire) {
    if (!ps4 || !direction || !fire) {
        return;
    }
    uint8_t right_dummy;
    ps4_compute_axes(ps4, direction, fire, &right_dummy, NULL);
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

uint8_t ps4_connected_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].connected) {
            count++;
        }
    }
    return count;
}

bool ps4_llamatron_axes(uint8_t* joy1_axis, uint8_t* joy1_fire,
                        uint8_t* joy0_axis, uint8_t* joy0_fire) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].connected) {
            ps4_compute_axes(&controllers[i], joy1_axis, joy1_fire, joy0_axis, joy0_fire);
            return true;
        }
    }
    return false;
}

