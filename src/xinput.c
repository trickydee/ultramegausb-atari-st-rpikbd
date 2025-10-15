/*
 * Atari ST RP2040 IKBD Emulator - Xbox Controller Support
 * Copyright (C) 2025
 * 
 * XInput Protocol Handler Implementation
 * 
 * Uses low-level TinyUSB endpoint transfers to communicate with Xbox controllers
 * Works with TinyUSB 0.19.0 without full vendor class support
 */

#include "xinput.h"
#include "tusb.h"
#include "host/usbh.h"
#include "host/usbh_pvt.h"
#include <stdio.h>
#include <string.h>

//--------------------------------------------------------------------
// Xbox Endpoints
//--------------------------------------------------------------------

// Xbox One typical endpoints
#define XBOX_EP_IN      0x81  // Interrupt IN endpoint
#define XBOX_EP_OUT     0x01  // Interrupt OUT endpoint

//--------------------------------------------------------------------
// Xbox Initialization Packet
//--------------------------------------------------------------------

// This packet must be sent to the Xbox controller to wake it up
// Sent to endpoint 0x01 (OUT)
const uint8_t xbox_init_packet[XBOX_INIT_PACKET_SIZE] = {
    0x05, 0x20, 0x00, 0x01, 0x00
};

// Buffer for receiving Xbox input reports
static uint8_t xbox_report_buffer[XBOX_INPUT_REPORT_SIZE];

//--------------------------------------------------------------------
// Controller Storage
//--------------------------------------------------------------------

#define MAX_XBOX_CONTROLLERS  2

static xbox_controller_t controllers[MAX_XBOX_CONTROLLERS];
static uint8_t controller_count = 0;

//--------------------------------------------------------------------
// Forward Declarations
//--------------------------------------------------------------------

static void xbox_init_complete_cb(tuh_xfer_t* xfer);
static void xbox_report_received_cb(tuh_xfer_t* xfer);

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
    
    // Step 1: Open endpoint descriptors for Xbox controller
    // Xbox uses interrupt endpoints (not control transfers)
    
    // Create endpoint descriptor for IN endpoint (receive data from Xbox)
    tusb_desc_endpoint_t ep_in_desc = {
        .bLength = sizeof(tusb_desc_endpoint_t),
        .bDescriptorType = TUSB_DESC_ENDPOINT,
        .bEndpointAddress = XBOX_EP_IN,  // 0x81
        .bmAttributes = {.xfer = TUSB_XFER_INTERRUPT},
        .wMaxPacketSize = 64,
        .bInterval = 4  // Poll every 4ms
    };
    
    // Create endpoint descriptor for OUT endpoint (send data to Xbox)
    tusb_desc_endpoint_t ep_out_desc = {
        .bLength = sizeof(tusb_desc_endpoint_t),
        .bDescriptorType = TUSB_DESC_ENDPOINT,
        .bEndpointAddress = XBOX_EP_OUT,  // 0x01
        .bmAttributes = {.xfer = TUSB_XFER_INTERRUPT},
        .wMaxPacketSize = 64,
        .bInterval = 4
    };
    
    // Step 2: Open endpoints
    if (tuh_edpt_open(dev_addr, &ep_in_desc)) {
        printf("Xbox: IN endpoint 0x%02X opened\n", XBOX_EP_IN);
    } else {
        printf("Xbox: Failed to open IN endpoint\n");
        return false;
    }
    
    if (tuh_edpt_open(dev_addr, &ep_out_desc)) {
        printf("Xbox: OUT endpoint 0x%02X opened\n", XBOX_EP_OUT);
    } else {
        printf("Xbox: Warning: Could not open OUT endpoint (may not be critical)\n");
    }
    
    // Step 3: Send initialization packet
    // Use tuh_edpt_xfer to send the init packet
    tuh_xfer_t xfer_out = {
        .daddr = dev_addr,
        .ep_addr = XBOX_EP_OUT,
        .buflen = XBOX_INIT_PACKET_SIZE,
        .buffer = (uint8_t*)xbox_init_packet,
        .complete_cb = xbox_init_complete_cb,
        .user_data = (uintptr_t)ctrl
    };
    
    if (tuh_edpt_xfer(&xfer_out)) {
        printf("Xbox: Init packet queued\n");
    } else {
        printf("Xbox: Init packet send failed\n");
    }
    
    ctrl->initialized = true;
    
    return true;
}

// Callback when initialization packet transfer completes
static void xbox_init_complete_cb(tuh_xfer_t* xfer) {
    if (xfer->result == XFER_RESULT_SUCCESS) {
        printf("Xbox: Init packet sent successfully! Controller should now send data.\n");
        
        // Start receiving input reports
        tuh_xfer_t xfer_in = {
            .daddr = xfer->daddr,
            .ep_addr = XBOX_EP_IN,
            .buflen = XBOX_INPUT_REPORT_SIZE,
            .buffer = xbox_report_buffer,
            .complete_cb = xbox_report_received_cb,
            .user_data = xfer->user_data
        };
        
        if (tuh_edpt_xfer(&xfer_in)) {
            printf("Xbox: Listening for input reports on endpoint 0x%02X\n", XBOX_EP_IN);
        } else {
            printf("Xbox: Failed to start listening for reports\n");
        }
    } else {
        printf("Xbox: Init packet failed with result %d\n", xfer->result);
    }
}

// Callback when Xbox input report is received
static void xbox_report_received_cb(tuh_xfer_t* xfer) {
    if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len > 0) {
        // Process the report
        xinput_process_report(xfer->daddr, xfer->buffer, xfer->actual_len);
        
        // Queue next report (continuous receiving)
        tuh_xfer_t xfer_in = {
            .daddr = xfer->daddr,
            .ep_addr = XBOX_EP_IN,
            .buflen = XBOX_INPUT_REPORT_SIZE,
            .buffer = xbox_report_buffer,
            .complete_cb = xbox_report_received_cb,
            .user_data = xfer->user_data
        };
        
        tuh_edpt_xfer(&xfer_in);  // Keep listening
    } else {
        printf("Xbox: Report receive failed, result=%d, len=%lu\n", xfer->result, xfer->actual_len);
    }
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
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  ��� XBOX CONTROLLER DETECTED!\n");
    printf("  Device Address: %d\n", dev_addr);
    printf("  \n");
    printf("  Attempting initialization with low-level USB API...\n");
    printf("  (TinyUSB 0.19.0 workaround - manual endpoint setup)\n");
    printf("  \n");
    printf("  Watch below for initialization progress:\n");
    printf("  \n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("\n");
    
    xinput_init_controller(dev_addr);
}

void xinput_unmount_cb(uint8_t dev_addr) {
    printf("Xbox: Controller unmounted at address %d\n", dev_addr);
    free_controller(dev_addr);
}

