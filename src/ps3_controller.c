/*
 * Atari ST RP2040 IKBD Emulator - PS3 DualShock 3 Support
 * Copyright (C) 2025
 * 
 * PS3 DualShock 3 controller implementation
 * Based on existing PS4 implementation and Linux kernel PS3 driver
 */

#include "ps3_controller.h"
#include "config.h"
#include "tusb.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>

//--------------------------------------------------------------------
// Controller Storage
//--------------------------------------------------------------------

#define MAX_PS3_CONTROLLERS  2

static ps3_controller_t controllers[MAX_PS3_CONTROLLERS];
static uint8_t controller_count = 0;

//--------------------------------------------------------------------
// Helper Functions
//--------------------------------------------------------------------

static ps3_controller_t* find_controller_by_addr(uint8_t dev_addr) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected) {
            return &controllers[i];
        }
    }
    return NULL;
}

static ps3_controller_t* allocate_controller(uint8_t dev_addr) {
    if (controller_count >= MAX_PS3_CONTROLLERS) {
        printf("PS3: Max controllers reached\n");
        return NULL;
    }
    
    ps3_controller_t* ctrl = &controllers[controller_count++];
    memset(ctrl, 0, sizeof(ps3_controller_t));
    ctrl->dev_addr = dev_addr;
    ctrl->connected = true;
    ctrl->deadzone = 50;  // Match PS4 deadzone
    
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

bool ps3_is_dualshock3(uint16_t vid, uint16_t pid) {
    if (vid != PS3_VENDOR_ID) {
        return false;
    }
    
    return (pid == PS3_DS3_PID);
}

bool ps3_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len) {
    static bool first_report_ever = true;
    extern ssd1306_t disp;
    
    ps3_controller_t* ctrl = find_controller_by_addr(dev_addr);
    if (!ctrl) {
        printf("PS3: Controller %d not found, allocating...\n", dev_addr);
        ctrl = allocate_controller(dev_addr);
        if (!ctrl) {
            return false;
        }
    }
    
    // Store raw report for debugging
    ctrl->raw_len = len < sizeof(ctrl->raw_report) ? len : sizeof(ctrl->raw_report);
    memcpy(ctrl->raw_report, report, ctrl->raw_len);
    
    // Show first report with detailed debug
    if (first_report_ever) {
        first_report_ever = false;
        printf("PS3: First report received (%d bytes)\n", len);
        
#if ENABLE_CONTROLLER_DEBUG
        // Show raw bytes on OLED for debugging
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 0, 0, 1, (char*)"PS3 First Rpt");
        
        char line[32];
        snprintf(line, sizeof(line), "Len:%d", len);
        ssd1306_draw_string(&disp, 0, 10, 1, line);
        
        // Show first 10 bytes
        for (int i = 0; i < 10 && i < len; i++) {
            snprintf(line, sizeof(line), "%d:%02X", i, report[i]);
            ssd1306_draw_string(&disp, (i % 5) * 25, 20 + (i / 5) * 10, 1, line);
        }
        
        ssd1306_show(&disp);
        sleep_ms(3000);
#endif
    }
    
    // PS3 report parsing - THIS IS THE KEY PART WE NEED TO DEBUG
    // Different PS3 controllers may have different report formats
    // We'll need to examine the actual bytes to determine the format
    
    // For now, implement a basic parser that we'll refine based on real data
    if (len >= 6) {
        // Try to extract analog sticks (typical positions in PS3 reports)
        // This is a guess - we'll need to adjust based on actual data
        
        // Common PS3 USB report format (based on Linux driver):
        // Byte 0: Report ID or first button byte
        // Bytes 1-2: More buttons
        // Bytes 6-9: Analog sticks (left X, left Y, right X, right Y)
        
        uint8_t offset = 0;
        
        // Check if first byte looks like report ID
        if (report[0] == 0x01) {
            offset = 1;
        }
        
        // Try to find stick data
        // PS3 sticks are typically at offset 6-9 (after buttons)
        if (len >= offset + 9) {
            ctrl->report.lx = report[offset + 6];
            ctrl->report.ly = report[offset + 7];
            ctrl->report.rx = report[offset + 8];
            ctrl->report.ry = report[offset + 9];
        }
        
        // D-Pad is usually in button byte as hat switch or individual bits
        if (len >= offset + 2) {
            ctrl->report.dpad = report[offset + 2] & 0x0F;  // Might be hat switch
        }
        
        // Store button bytes
        if (len >= offset + 3) {
            ctrl->report.buttons[0] = report[offset + 0];
            ctrl->report.buttons[1] = report[offset + 1];
            ctrl->report.buttons[2] = report[offset + 2];
        }
        
        // Triggers (if present)
        if (len >= offset + 20) {
            // PS3 has pressure-sensitive buttons, triggers might be further in report
            ctrl->report.l2_trigger = report[offset + 18];
            ctrl->report.r2_trigger = report[offset + 19];
        }
    }
    
#if ENABLE_CONTROLLER_DEBUG
    // Debug output every 100 reports (~once per second at 250Hz)
    static uint32_t report_count = 0;
    report_count++;
    
    if ((report_count % 100) == 0) {
        // Show current values on OLED
        ssd1306_clear(&disp);
        char line[32];
        
        snprintf(line, sizeof(line), "PS3 #%lu", report_count);
        ssd1306_draw_string(&disp, 0, 0, 1, line);
        
        snprintf(line, sizeof(line), "LX:%02X LY:%02X", ctrl->report.lx, ctrl->report.ly);
        ssd1306_draw_string(&disp, 0, 12, 1, line);
        
        snprintf(line, sizeof(line), "RX:%02X RY:%02X", ctrl->report.rx, ctrl->report.ry);
        ssd1306_draw_string(&disp, 0, 24, 1, line);
        
        snprintf(line, sizeof(line), "B:%02X %02X %02X D:%02X", 
                 ctrl->report.buttons[0], ctrl->report.buttons[1], 
                 ctrl->report.buttons[2], ctrl->report.dpad);
        ssd1306_draw_string(&disp, 0, 36, 1, line);
        
        ssd1306_show(&disp);
    }
#endif
    
    return true;
}

ps3_controller_t* ps3_get_controller(uint8_t dev_addr) {
    return find_controller_by_addr(dev_addr);
}

void ps3_to_atari(const ps3_controller_t* ps3, uint8_t joystick_num,
                  uint8_t* direction, uint8_t* fire) {
    if (!ps3 || !direction || !fire) {
        return;
    }
    
    const ps3_report_t* input = &ps3->report;
    *direction = 0;
    *fire = 0;
    
    // Convert analog stick to directions (left stick)
    // PS3 sticks are 0-255 with 128 as center (like PS4)
    int8_t stick_x = (int8_t)(input->lx - 128);
    int8_t stick_y = (int8_t)(input->ly - 128);
    
    // Apply deadzone
    if (stick_x < -ps3->deadzone || stick_x > ps3->deadzone ||
        stick_y < -ps3->deadzone || stick_y > ps3->deadzone) {
        
        if (stick_y < -ps3->deadzone) *direction |= 0x01;  // Up
        if (stick_y > ps3->deadzone)  *direction |= 0x02;  // Down
        if (stick_x < -ps3->deadzone) *direction |= 0x04;  // Left
        if (stick_x > ps3->deadzone)  *direction |= 0x08;  // Right
    }
    
    // D-Pad takes priority (if we can detect it)
    // This will need adjustment based on actual report format
    
    // Fire button - we'll need to figure out which button byte/bit
    // For now, assume X button (most common fire button)
    // This will need adjustment based on actual report format
    
    // Placeholder - will be refined during testing
    *fire = 0;
}

void ps3_set_deadzone(uint8_t dev_addr, int16_t deadzone) {
    ps3_controller_t* ctrl = find_controller_by_addr(dev_addr);
    if (ctrl) {
        ctrl->deadzone = deadzone;
        printf("PS3: Deadzone set to %d for controller %d\n", deadzone, dev_addr);
    }
}

void ps3_mount_cb(uint8_t dev_addr) {
    extern ssd1306_t disp;
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  🎮 PS3 DUALSHOCK 3 DETECTED!\n");
    printf("  Device Address: %d\n", dev_addr);
    printf("  \n");
    printf("  PS3 controllers may require special initialization\n");
    printf("  Debug mode active - will show report data\n");
    printf("  \n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Show on OLED
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 25, 10, 2, (char*)"PS3");
    ssd1306_draw_string(&disp, 10, 35, 1, (char*)"DualShock 3");
    
    // Show debug info: Address
    char debug_line[20];
    snprintf(debug_line, sizeof(debug_line), "Addr:%d", dev_addr);
    ssd1306_draw_string(&disp, 25, 50, 1, debug_line);
    
    ssd1306_show(&disp);
    sleep_ms(2000);
    
    ps3_controller_t* ctrl = allocate_controller(dev_addr);
    if (ctrl) {
        printf("PS3: Controller registered and ready for testing!\n");
    }
}

void ps3_unmount_cb(uint8_t dev_addr) {
    printf("PS3: Controller unmounted at address %d\n", dev_addr);
    free_controller(dev_addr);
}

