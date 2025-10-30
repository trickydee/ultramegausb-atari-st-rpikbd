/*
 * Atari ST RP2040 IKBD Emulator - Google Stadia Controller Support
 * Copyright (C) 2025
 * 
 * Google Stadia controller implementation
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef STADIA_CONTROLLER_H
#define STADIA_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// Google Stadia Controller Identification
//--------------------------------------------------------------------

// Google Vendor ID
#define STADIA_VENDOR_ID        0x18D1

// Stadia Controller Product IDs
#define STADIA_CONTROLLER       0x9400  // Stadia Controller rev. A

//--------------------------------------------------------------------
// Stadia Controller State
//--------------------------------------------------------------------

#define MAX_STADIA_CONTROLLERS  2

typedef struct {
    uint8_t dev_addr;           // USB device address
    uint8_t instance;           // Instance number
    bool    connected;          // Connection status
    
    // Parsed input state
    uint16_t buttons;           // Button bit field
    int16_t  stick_left_x;      // Left stick X
    int16_t  stick_left_y;      // Left stick Y
    int16_t  stick_right_x;     // Right stick X
    int16_t  stick_right_y;     // Right stick Y
    uint8_t  trigger_left;      // Left trigger (0-255)
    uint8_t  trigger_right;     // Right trigger (0-255)
    uint8_t  dpad;              // D-Pad state (hat switch)
    
    // Configuration
    int16_t  deadzone;          // Stick deadzone (default 20)
    
} stadia_controller_t;

//--------------------------------------------------------------------
// Stadia Button Definitions (standard gamepad layout)
//--------------------------------------------------------------------

#define STADIA_BTN_A            0x0001
#define STADIA_BTN_B            0x0002
#define STADIA_BTN_X            0x0004
#define STADIA_BTN_Y            0x0008
#define STADIA_BTN_L1           0x0010  // Left shoulder
#define STADIA_BTN_R1           0x0020  // Right shoulder
#define STADIA_BTN_L2           0x0040  // Left trigger (digital)
#define STADIA_BTN_R2           0x0080  // Right trigger (digital)
#define STADIA_BTN_SELECT       0x0100  // Options/Menu
#define STADIA_BTN_START        0x0200  // Menu/Hamburger
#define STADIA_BTN_L3           0x0400  // Left stick click
#define STADIA_BTN_R3           0x0800  // Right stick click
#define STADIA_BTN_HOME         0x1000  // Stadia button
#define STADIA_BTN_CAPTURE      0x2000  // Google Assistant button

// D-Pad values (hat switch)
#define STADIA_DPAD_UP          0
#define STADIA_DPAD_UP_RIGHT    1
#define STADIA_DPAD_RIGHT       2
#define STADIA_DPAD_DOWN_RIGHT  3
#define STADIA_DPAD_DOWN        4
#define STADIA_DPAD_DOWN_LEFT   5
#define STADIA_DPAD_LEFT        6
#define STADIA_DPAD_UP_LEFT     7
#define STADIA_DPAD_NEUTRAL     15

//--------------------------------------------------------------------
// API Functions
//--------------------------------------------------------------------

/**
 * Check if a device is a Stadia controller
 * @param vid Vendor ID
 * @param pid Product ID
 * @return true if device is Stadia controller
 */
bool stadia_is_controller(uint16_t vid, uint16_t pid);

/**
 * Get controller by device address
 * @param dev_addr USB device address
 * @return Pointer to controller (NULL if not found)
 */
stadia_controller_t* stadia_get_controller(uint8_t dev_addr);

/**
 * Process Stadia controller HID report
 * @param dev_addr USB device address
 * @param report Pointer to HID report data
 * @param len Report length
 */
void stadia_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len);

/**
 * Convert Stadia input to Atari joystick format
 * @param stadia Stadia controller state
 * @param joystick_num Atari joystick number (0 or 1)
 * @param direction Output: direction byte
 * @param fire Output: fire button state
 */
void stadia_to_atari(const stadia_controller_t* stadia, uint8_t joystick_num, 
                     uint8_t* direction, uint8_t* fire);

/**
 * Mount callback - called when Stadia controller connected
 * @param dev_addr USB device address
 */
void stadia_mount_cb(uint8_t dev_addr);

/**
 * Unmount callback - called when Stadia controller disconnected
 * @param dev_addr USB device address
 */
void stadia_unmount_cb(uint8_t dev_addr);

/**
 * Set stick deadzone
 * @param dev_addr USB device address
 * @param deadzone Deadzone value (0-127, default 20)
 */
void stadia_set_deadzone(uint8_t dev_addr, int16_t deadzone);

#ifdef __cplusplus
}
#endif

#endif /* STADIA_CONTROLLER_H */


