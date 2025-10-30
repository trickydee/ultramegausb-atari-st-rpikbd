/*
 * Atari ST RP2040 IKBD Emulator - PS3 DualShock 3 Support
 * Copyright (C) 2025
 * 
 * PS3 DualShock 3 controller support for Atari ST joystick emulation
 * Based on existing PS4 implementation and Linux kernel PS3 driver
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef PS3_CONTROLLER_H
#define PS3_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// PS3 DualShock 3 Identification
//--------------------------------------------------------------------

// Sony Vendor ID (same as PS4)
#define PS3_VENDOR_ID       0x054C

// DualShock 3 Product IDs
#define PS3_DS3_PID         0x0268  // DualShock 3 standard

//--------------------------------------------------------------------
// PS3 DualShock 3 Button Definitions
// Based on Linux kernel driver and reference implementation
//--------------------------------------------------------------------

// D-Pad buttons (in button array)
#define PS3_BTN_ARROW_UP        4
#define PS3_BTN_ARROW_RIGHT     5
#define PS3_BTN_ARROW_DOWN      6
#define PS3_BTN_ARROW_LEFT      7

// Face buttons
#define PS3_BTN_TRIANGLE       12
#define PS3_BTN_CIRCLE         13
#define PS3_BTN_X              14
#define PS3_BTN_SQUARE         15

// Shoulder buttons
#define PS3_BTN_L1             10
#define PS3_BTN_R1             11
#define PS3_BTN_L2              8
#define PS3_BTN_R2              9

// Stick buttons
#define PS3_BTN_L3              1
#define PS3_BTN_R3              2

// System buttons
#define PS3_BTN_SELECT          0
#define PS3_BTN_START           3
#define PS3_BTN_PS             16

//--------------------------------------------------------------------
// PS3 DualShock 3 Report Structure
//--------------------------------------------------------------------

// PS3 report format is similar to PS4 but with different layout
// Note: PS3 may use different report formats over USB vs Bluetooth
typedef struct TU_ATTR_PACKED {
    // Button states (may be in bitmask or array format)
    uint8_t buttons[3];         // Button states (varies by report type)
    
    // Analog sticks (0-255, 128 = center)
    uint8_t lx;                 // Left stick X
    uint8_t ly;                 // Left stick Y
    uint8_t rx;                 // Right stick X
    uint8_t ry;                 // Right stick Y
    
    // D-Pad (may be hat switch format: 0-7, 8=center)
    uint8_t dpad;
    
    // Analog triggers (0-255)
    uint8_t l2_trigger;
    uint8_t r2_trigger;
    
    // Additional pressure-sensitive button data follows
    // (not needed for basic joystick control)
} ps3_report_t;

//--------------------------------------------------------------------
// PS3 Controller State
//--------------------------------------------------------------------

typedef struct {
    uint8_t dev_addr;           // USB device address
    bool connected;             // Connection status
    ps3_report_t report;        // Latest report
    int16_t deadzone;           // Stick deadzone (default 50)
    uint8_t raw_report[64];     // Store raw report for debugging
    uint16_t raw_len;           // Raw report length
} ps3_controller_t;

//--------------------------------------------------------------------
// API Functions
//--------------------------------------------------------------------

/**
 * Check if a device is a PS3 DualShock 3 controller
 * @param vid Vendor ID
 * @param pid Product ID
 * @return true if device is PS3 controller
 */
bool ps3_is_dualshock3(uint16_t vid, uint16_t pid);

/**
 * Process PS3 controller report
 * @param dev_addr USB device address
 * @param report Pointer to HID report data
 * @param len Report length
 * @return true if processed successfully
 */
bool ps3_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len);

/**
 * Get PS3 controller state
 * @param dev_addr USB device address
 * @return Pointer to controller state (NULL if not found)
 */
ps3_controller_t* ps3_get_controller(uint8_t dev_addr);

/**
 * Convert PS3 input to Atari joystick format
 * @param ps3 PS3 controller state
 * @param joystick_num Atari joystick number (0 or 1)
 * @param direction Output: direction byte (bit 0-3: up/down/left/right)
 * @param fire Output: fire button state (0 or 1)
 */
void ps3_to_atari(const ps3_controller_t* ps3, uint8_t joystick_num,
                  uint8_t* direction, uint8_t* fire);

/**
 * Set stick deadzone
 * @param dev_addr USB device address
 * @param deadzone Deadzone value (0-127, default 50)
 */
void ps3_set_deadzone(uint8_t dev_addr, int16_t deadzone);

/**
 * Mount callback
 * @param dev_addr USB device address
 */
void ps3_mount_cb(uint8_t dev_addr);

/**
 * Unmount callback
 * @param dev_addr USB device address
 */
void ps3_unmount_cb(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif /* PS3_CONTROLLER_H */

