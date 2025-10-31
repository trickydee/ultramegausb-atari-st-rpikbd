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
    
    // PS3 DualShock 3 report parsing
    // Based on actual hardware testing
    // Report format (48 bytes total):
    // Byte 0: Report ID (0x01)
    // Byte 1: Buttons (SELECT=0x01, L3=0x02, R3=0x04, START=0x08, etc)
    // Byte 2: D-Pad (UP=0x10, RIGHT=0x20, DOWN=0x40, LEFT=0x80)
    // Byte 3: Buttons (L2=0x01, R2=0x02, L1=0x04, R1=0x08, Triangle=0x10, Circle=0x20, X=0x40, Square=0x80)
    // Bytes 6-9: Analog sticks (LX, LY, RX, RY) - 0x80 = center
    // Bytes 18-19: Analog triggers (L2, R2) - 0x00 = released, 0xFF = fully pressed
    
    if (len >= 20) {
        uint8_t offset = 0;
        
        // Check if first byte is report ID
        if (report[0] == 0x01) {
            offset = 1;
            // Without offset: report[0] is report ID
            // With offset=1: we skip it and read from report[1], report[2], etc.
        } else {
            // If no report ID, data starts at byte 0
            offset = 0;
        }
        
        // Button bytes
        ctrl->report.buttons[0] = report[offset + 0];  // SELECT, L3, R3, START
        ctrl->report.buttons[1] = report[offset + 1];  // D-Pad
        ctrl->report.buttons[2] = report[offset + 2];  // L2, R2, L1, R1, Triangle, Circle, X, Square
        
        // Analog sticks (bytes 6-9 in report, or 5-8 if offset=1)
        // 0x00 = full left/up, 0x80 = center, 0xFF = full right/down
        if (len >= offset + 9) {
            ctrl->report.lx = report[offset + 5];  // Left stick X
            ctrl->report.ly = report[offset + 6];  // Left stick Y  
            ctrl->report.rx = report[offset + 7];  // Right stick X
            ctrl->report.ry = report[offset + 8];  // Right stick Y
        }
        
        // Analog triggers (bytes 18-19, or 17-18 if offset=1)
        // 0x00 = released, 0xFF = fully pressed
        if (len >= offset + 19) {
            ctrl->report.l2_trigger = report[offset + 17];
            ctrl->report.r2_trigger = report[offset + 18];
        }
        
        // Extract D-Pad from byte 1 (bitmask format)
        // UP=0x10, RIGHT=0x20, DOWN=0x40, LEFT=0x80
        uint8_t dpad_byte = ctrl->report.buttons[1];
        ctrl->report.dpad = 8;  // Default = centered
        
        // Convert bitmask to hat switch format (0-7, 8=center)
        if (dpad_byte & 0x10) {  // UP
            if (dpad_byte & 0x20) ctrl->report.dpad = 1;      // UP-RIGHT
            else if (dpad_byte & 0x80) ctrl->report.dpad = 7; // UP-LEFT  
            else ctrl->report.dpad = 0;                       // UP
        } else if (dpad_byte & 0x40) {  // DOWN
            if (dpad_byte & 0x20) ctrl->report.dpad = 3;      // DOWN-RIGHT
            else if (dpad_byte & 0x80) ctrl->report.dpad = 5; // DOWN-LEFT
            else ctrl->report.dpad = 4;                       // DOWN
        } else if (dpad_byte & 0x20) {  // RIGHT
            ctrl->report.dpad = 2;
        } else if (dpad_byte & 0x80) {  // LEFT
            ctrl->report.dpad = 6;
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
    // PS3 sticks are 0-255 with 128/0x80 as center
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
    
    // D-Pad takes priority if pressed (converted to hat switch format 0-7, 8=center)
    if (input->dpad != 8) {
        *direction = 0;
        switch (input->dpad) {
            case 0: *direction = 0x01; break;  // UP
            case 1: *direction = 0x09; break;  // UP-RIGHT
            case 2: *direction = 0x08; break;  // RIGHT
            case 3: *direction = 0x0A; break;  // DOWN-RIGHT
            case 4: *direction = 0x02; break;  // DOWN
            case 5: *direction = 0x06; break;  // DOWN-LEFT
            case 6: *direction = 0x04; break;  // LEFT
            case 7: *direction = 0x05; break;  // UP-LEFT
        }
    }
    
    // Fire button mapping
    // buttons[2] contains: L2=0x01, R2=0x02, L1=0x04, R1=0x08, 
    //                      Triangle=0x10, Circle=0x20, X=0x40, Square=0x80
    
    // Fire: X button (Cross) only
    if (input->buttons[2] & 0x40) {
        *fire = 1;
    }
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
    printf("  Sending PS3 initialization command...\n");
    printf("  \n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Show on OLED - match Xbox/PS4/Switch style
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 25, 10, 2, (char*)"PS3");
    ssd1306_draw_string(&disp, 10, 35, 1, (char*)"DualShock 3");
    ssd1306_show(&disp);
    sleep_ms(2000);
    
    ps3_controller_t* ctrl = allocate_controller(dev_addr);
    if (ctrl) {
        printf("PS3: Controller registered!\n");
        
        // PS3 DualShock 3 requires special initialization
        // Send Feature Report 0xF4 to enable the controller
        // This stops the 4 flashing lights and activates the controller
        static const uint8_t ps3_init_report[] = {
            0x42, 0x0C, 0x00, 0x00  // PS3 enable command
        };
        
        printf("PS3: Sending initialization feature report (0xF4)...\n");
        
        // Send feature report to initialize controller
        // Report ID 0xF4, data length 4 bytes
        bool result = tuh_hid_set_report(dev_addr, 0, // instance 0
                                          0xF4,        // report_id
                                          HID_REPORT_TYPE_FEATURE,
                                          (uint8_t*)ps3_init_report, 
                                          sizeof(ps3_init_report));
        
        if (result) {
            printf("PS3: Initialization sent successfully!\n");
            printf("PS3: Controller should activate (lights stop flashing)\n");
        } else {
            printf("PS3: WARNING - Initialization send failed!\n");
            printf("PS3: Controller may not work properly\n");
        }
    }
}

void ps3_unmount_cb(uint8_t dev_addr) {
    printf("PS3: Controller unmounted at address %d\n", dev_addr);
    free_controller(dev_addr);
}

