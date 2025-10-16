/*
 * Atari ST RP2040 IKBD Emulator - Xbox Controller Support
 * Copyright (C) 2025
 * 
 * XInput Protocol Handler for Xbox One Controllers
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef XINPUT_H
#define XINPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// Xbox Controller Identification
//--------------------------------------------------------------------

// Microsoft Vendor ID
#define XBOX_VENDOR_ID      0x045E

// Xbox One Controller Product IDs
#define XBOX_ONE_PID_1      0x02DD  // Xbox One Controller
#define XBOX_ONE_PID_2      0x02E3  // Xbox One Elite Controller
#define XBOX_ONE_PID_3      0x02EA  // Xbox One Controller
#define XBOX_ONE_PID_4      0x02FD  // Xbox One S Controller
#define XBOX_ONE_PID_5      0x0B00  // Xbox One Elite Series 2
#define XBOX_ONE_PID_6      0x0B05  // Xbox One Elite Controller
#define XBOX_ONE_PID_7      0x0B12  // Xbox Series X|S Controller

// USB Class/Subclass for Xbox
#define XBOX_USB_CLASS      0xFF    // Vendor-specific
#define XBOX_USB_SUBCLASS   0x5D    // Xbox 360 peripherals
#define XBOX_USB_PROTOCOL   0x01    // Controller for Xbox One

//--------------------------------------------------------------------
// XInput Protocol Definitions
//--------------------------------------------------------------------

// Xbox One initialization packet (sent to endpoint 0x01)
#define XBOX_INIT_PACKET_SIZE   5
extern const uint8_t xbox_init_packet[XBOX_INIT_PACKET_SIZE];

// Report IDs
#define XBOX_REPORT_ID_INPUT    0x20  // Input report
#define XBOX_REPORT_ID_GUIDE    0x07  // Guide button report

// Report sizes
#define XBOX_INPUT_REPORT_SIZE  64    // Total input report size

//--------------------------------------------------------------------
// Button Definitions
//--------------------------------------------------------------------

// Button bit masks (in buttons field)
#define XBOX_BTN_DPAD_UP        0x0001
#define XBOX_BTN_DPAD_DOWN      0x0002
#define XBOX_BTN_DPAD_LEFT      0x0004
#define XBOX_BTN_DPAD_RIGHT     0x0008
#define XBOX_BTN_START          0x0010
#define XBOX_BTN_BACK           0x0020  // View/Select
#define XBOX_BTN_LS             0x0040  // Left stick click
#define XBOX_BTN_RS             0x0080  // Right stick click
#define XBOX_BTN_LB             0x0100  // Left bumper
#define XBOX_BTN_RB             0x0200  // Right bumper
#define XBOX_BTN_GUIDE          0x0400  // Xbox button (via separate report)
#define XBOX_BTN_A              0x1000
#define XBOX_BTN_B              0x2000
#define XBOX_BTN_X              0x4000
#define XBOX_BTN_Y              0x8000

//--------------------------------------------------------------------
// Xbox Input Report Structure
//--------------------------------------------------------------------

typedef struct TU_ATTR_PACKED {
    uint8_t  report_id;         // Always 0x20 for input
    uint8_t  size;              // Report size (usually 0x0E)
    
    // Buttons (2 bytes)
    uint16_t buttons;           // Button bit field (see XBOX_BTN_* above)
    
    // Triggers (2 bytes) - 10-bit values, left-aligned in 16-bit fields
    uint16_t trigger_left;      // 0-1023 (in upper 10 bits)
    uint16_t trigger_right;     // 0-1023 (in upper 10 bits)
    
    // Left stick (4 bytes)
    int16_t stick_left_x;       // -32768 to 32767
    int16_t stick_left_y;       // -32768 to 32767
    
    // Right stick (4 bytes)
    int16_t stick_right_x;      // -32768 to 32767
    int16_t stick_right_y;      // -32768 to 32767
    
} xbox_input_report_t;

//--------------------------------------------------------------------
// Xbox Controller State
//--------------------------------------------------------------------

typedef struct {
    uint8_t dev_addr;           // USB device address
    uint8_t instance;           // Instance number
    bool    connected;          // Connection status
    bool    initialized;        // Init packet sent
    
    // Parsed input state
    xbox_input_report_t report;
    
    // Configuration
    int16_t deadzone;           // Stick deadzone (default 8000 = ~25%)
    
} xbox_controller_t;

//--------------------------------------------------------------------
// API Functions
//--------------------------------------------------------------------

/**
 * Check if a device is an Xbox controller
 * @param vid Vendor ID
 * @param pid Product ID
 * @return true if device is Xbox controller
 */
bool xinput_is_xbox_controller(uint16_t vid, uint16_t pid);

/**
 * Initialize Xbox controller
 * @param dev_addr USB device address
 * @return true if initialization started
 */
bool xinput_init_controller(uint8_t dev_addr);

/**
 * Process Xbox input report
 * @param dev_addr USB device address
 * @param report Pointer to raw report data
 * @param len Report length
 * @return true if report processed successfully
 */
bool xinput_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len);

/**
 * Get Xbox controller state
 * @param dev_addr USB device address
 * @return Pointer to controller state (NULL if not found)
 */
xbox_controller_t* xinput_get_controller(uint8_t dev_addr);

/**
 * Convert Xbox input to Atari joystick format
 * @param xbox Xbox controller state
 * @param joystick_num Atari joystick number (0 or 1)
 * @param direction Output: direction byte (bit 0-3: up/down/left/right)
 * @param fire Output: fire button state (0 or 1)
 */
void xinput_to_atari(const xbox_controller_t* xbox, uint8_t joystick_num, 
                     uint8_t* direction, uint8_t* fire);

/**
 * Set stick deadzone
 * @param dev_addr USB device address
 * @param deadzone Deadzone value (0-32767, default 8000)
 */
void xinput_set_deadzone(uint8_t dev_addr, int16_t deadzone);

/**
 * Mount callback - called when Xbox controller connected
 * @param dev_addr USB device address
 */
void xinput_mount_cb(uint8_t dev_addr);

/**
 * Unmount callback - called when Xbox controller disconnected
 * @param dev_addr USB device address
 */
void xinput_unmount_cb(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif /* XINPUT_H */

