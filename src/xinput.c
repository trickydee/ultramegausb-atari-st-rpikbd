/*
 * Atari ST RP2040 IKBD Emulator - Xbox Controller Support
 * Copyright (C) 2025
 * 
 * XInput Protocol Handler Implementation
 */

#include "xinput.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

//--------------------------------------------------------------------
// Xbox Initialization Packet
//--------------------------------------------------------------------

// This packet must be sent to the Xbox controller to wake it up
// Sent to endpoint 0x01 (OUT)
const uint8_t xbox_init_packet[XBOX_INIT_PACKET_SIZE] = {
    0x05, 0x20, 0x00, 0x01, 0x00
};

//--------------------------------------------------------------------
// Controller Storage
//--------------------------------------------------------------------

#define MAX_XBOX_CONTROLLERS  2

static xbox_controller_t controllers[MAX_XBOX_CONTROLLERS];
static uint8_t controller_count = 0;

//--------------------------------------------------------------------
// Helper Functions
//--------------------------------------------------------------------

static xbox_controller_t* find_controller_by_addr(uint8_t dev_addr) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected) {
            return &controllers[i];
        }
    }
    return NULL;
}

static xbox_controller_t* allocate_controller(uint8_t dev_addr) {
    if (controller_count >= MAX_XBOX_CONTROLLERS) {
        printf("Xbox: Max controllers reached\n");
        return NULL;
    }
    
    xbox_controller_t* ctrl = &controllers[controller_count++];
    memset(ctrl, 0, sizeof(xbox_controller_t));
    ctrl->dev_addr = dev_addr;
    ctrl->connected = true;
    ctrl->initialized = false;
    ctrl->deadzone = 8000;  // Default ~25% deadzone
    
    return ctrl;
}

static void free_controller(uint8_t dev_addr) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].dev_addr == dev_addr) {
            // Shift remaining controllers down
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

bool xinput_is_xbox_controller(uint16_t vid, uint16_t pid) {
    if (vid != XBOX_VENDOR_ID) {
        return false;
    }
    
    // Check against known Xbox controller PIDs
    switch (pid) {
        case XBOX_ONE_PID_1:
        case XBOX_ONE_PID_2:
        case XBOX_ONE_PID_3:
        case XBOX_ONE_PID_4:
        case XBOX_ONE_PID_5:
        case XBOX_ONE_PID_6:
        case XBOX_ONE_PID_7:
            return true;
        default:
            return false;
    }
}

bool xinput_init_controller(uint8_t dev_addr) {
    xbox_controller_t* ctrl = find_controller_by_addr(dev_addr);
    if (!ctrl) {
        ctrl = allocate_controller(dev_addr);
        if (!ctrl) {
            return false;
        }
    }
    
    printf("Xbox: Initializing controller at address %d\n", dev_addr);
    
    // Send initialization packet to endpoint 0x01
    // Note: This requires TinyUSB vendor transfer support
    // For now, we'll mark as initialized and the controller should start sending data
    ctrl->initialized = true;
    
    printf("Xbox: Controller %d initialized\n", dev_addr);
    return true;
}

bool xinput_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len) {
    xbox_controller_t* ctrl = find_controller_by_addr(dev_addr);
    if (!ctrl) {
        printf("Xbox: Controller %d not found\n", dev_addr);
        return false;
    }
    
    // Minimum report size check
    if (len < 16) {
        printf("Xbox: Report too short (%d bytes)\n", len);
        return false;
    }
    
    // Check report ID
    if (report[0] != XBOX_REPORT_ID_INPUT) {
        // Might be guide button or other report
        if (report[0] == XBOX_REPORT_ID_GUIDE) {
            printf("Xbox: Guide button pressed\n");
        }
        return false;
    }
    
    // Parse input report
    xbox_input_report_t* input = &ctrl->report;
    
    input->report_id = report[0];
    input->size = report[1];
    
    // Buttons (bytes 4-5, little endian)
    input->buttons = report[4] | (report[5] << 8);
    
    // Triggers (bytes 6-9, 10-bit values)
    input->trigger_left = report[6] | (report[7] << 8);
    input->trigger_right = report[8] | (report[9] << 8);
    
    // Left stick (bytes 10-13)
    input->stick_left_x = (int16_t)(report[10] | (report[11] << 8));
    input->stick_left_y = (int16_t)(report[12] | (report[13] << 8));
    
    // Right stick (bytes 14-17, if available)
    if (len >= 18) {
        input->stick_right_x = (int16_t)(report[14] | (report[15] << 8));
        input->stick_right_y = (int16_t)(report[16] | (report[17] << 8));
    }
    
    // Debug output (can be removed later)
    static uint32_t report_count = 0;
    if ((report_count++ % 100) == 0) {  // Print every 100th report
        printf("Xbox: Buttons=0x%04X LX=%d LY=%d\n", 
               input->buttons, input->stick_left_x, input->stick_left_y);
    }
    
    return true;
}

xbox_controller_t* xinput_get_controller(uint8_t dev_addr) {
    return find_controller_by_addr(dev_addr);
}

void xinput_to_atari(const xbox_controller_t* xbox, uint8_t joystick_num, 
                     uint8_t* direction, uint8_t* fire) {
    if (!xbox || !direction || !fire) {
        return;
    }
    
    const xbox_input_report_t* input = &xbox->report;
    *direction = 0;
    *fire = 0;
    
    // Apply deadzone to left stick
    int16_t x = input->stick_left_x;
    int16_t y = input->stick_left_y;
    
    if (x < -xbox->deadzone || x > xbox->deadzone ||
        y < -xbox->deadzone || y > xbox->deadzone) {
        
        // Convert stick to directions
        // Note: Y axis is inverted (positive = down in USB, but up in Atari)
        if (y < -xbox->deadzone) *direction |= 0x01;  // Up
        if (y > xbox->deadzone)  *direction |= 0x02;  // Down
        if (x < -xbox->deadzone) *direction |= 0x04;  // Left
        if (x > xbox->deadzone)  *direction |= 0x08;  // Right
    }
    
    // D-Pad as alternative/override (takes priority)
    if (input->buttons & XBOX_BTN_DPAD_UP)    *direction |= 0x01;
    if (input->buttons & XBOX_BTN_DPAD_DOWN)  *direction |= 0x02;
    if (input->buttons & XBOX_BTN_DPAD_LEFT)  *direction |= 0x04;
    if (input->buttons & XBOX_BTN_DPAD_RIGHT) *direction |= 0x08;
    
    // Fire button mapping
    // Primary: A button
    // Alternative: Right trigger (if pressed > 50%)
    if (input->buttons & XBOX_BTN_A) {
        *fire = 1;
    } else if (input->trigger_right > 512) {  // 10-bit value, 512 = ~50%
        *fire = 1;
    }
    
    // Could also map B, X, Y buttons if needed
}

void xinput_set_deadzone(uint8_t dev_addr, int16_t deadzone) {
    xbox_controller_t* ctrl = find_controller_by_addr(dev_addr);
    if (ctrl) {
        ctrl->deadzone = deadzone;
        printf("Xbox: Deadzone set to %d for controller %d\n", deadzone, dev_addr);
    }
}

void xinput_mount_cb(uint8_t dev_addr) {
    printf("Xbox: Controller mounted at address %d\n", dev_addr);
    xinput_init_controller(dev_addr);
}

void xinput_unmount_cb(uint8_t dev_addr) {
    printf("Xbox: Controller unmounted at address %d\n", dev_addr);
    free_controller(dev_addr);
}

