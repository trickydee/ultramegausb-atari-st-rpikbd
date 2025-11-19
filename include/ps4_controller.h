/*
 * Atari ST RP2040 IKBD Emulator - PS4 DualShock 4 Support
 * Copyright (C) 2025
 * 
 * PS4 DualShock 4 controller support for Atari ST joystick emulation
 * Based on TinyUSB HID controller example
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef PS4_CONTROLLER_H
#define PS4_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// PS4 DualShock 4 Identification
//--------------------------------------------------------------------

// Sony Vendor ID
#define PS4_VENDOR_ID       0x054C

// DualShock 4 Product IDs
#define PS4_DS4_PID_V1      0x05C4  // DualShock 4 v1
#define PS4_DS4_PID_V2      0x09CC  // DualShock 4 v2
#define PS4_DS4_PID_DONGLE  0x0BA0  // DualShock 4 USB Wireless Adaptor

//--------------------------------------------------------------------
// PS4 DualShock 4 Report Structure
//--------------------------------------------------------------------

// PS4 uses standard HID, but report format is Sony-specific
typedef struct TU_ATTR_PACKED {
    uint8_t x, y, z, rz;        // Analog sticks (0-255, 128 = center)
    
    uint8_t dpad : 4;           // D-Pad (0-7 = directions, 8 = center)
    uint8_t square : 1;         // Square button
    uint8_t cross : 1;          // Cross (X) button  
    uint8_t circle : 1;         // Circle button
    uint8_t triangle : 1;       // Triangle button
    
    uint8_t l1 : 1;             // L1 button
    uint8_t r1 : 1;             // R1 button
    uint8_t l2 : 1;             // L2 button
    uint8_t r2 : 1;             // R2 button
    uint8_t share : 1;          // Share button
    uint8_t options : 1;        // Options button
    uint8_t l3 : 1;             // L3 (left stick click)
    uint8_t r3 : 1;             // R3 (right stick click)
    
    uint8_t ps : 1;             // PS button
    uint8_t tpad : 1;           // Touchpad click
    uint8_t counter : 6;        // Report counter
    
    uint8_t l2_trigger;         // L2 analog trigger (0-255)
    uint8_t r2_trigger;         // R2 analog trigger (0-255)
    
    // Additional fields exist but not needed for basic joystick control
} ps4_report_t;

// D-Pad direction values
#define PS4_DPAD_UP         0
#define PS4_DPAD_UP_RIGHT   1
#define PS4_DPAD_RIGHT      2
#define PS4_DPAD_DOWN_RIGHT 3
#define PS4_DPAD_DOWN       4
#define PS4_DPAD_DOWN_LEFT  5
#define PS4_DPAD_LEFT       6
#define PS4_DPAD_UP_LEFT    7
#define PS4_DPAD_CENTER     8

//--------------------------------------------------------------------
// PS4 Controller State
//--------------------------------------------------------------------

typedef struct {
    uint8_t dev_addr;           // USB device address
    bool connected;             // Connection status
    ps4_report_t report;        // Latest report
    int16_t deadzone;           // Stick deadzone (default 50 = ~39% for drift compensation)
} ps4_controller_t;

//--------------------------------------------------------------------
// API Functions
//--------------------------------------------------------------------

/**
 * Check if a device is a PS4 DualShock 4 controller
 * @param vid Vendor ID
 * @param pid Product ID
 * @return true if device is PS4 controller
 */
bool ps4_is_dualshock4(uint16_t vid, uint16_t pid);

/**
 * Process PS4 controller report
 * @param dev_addr USB device address
 * @param report Pointer to HID report data
 * @param len Report length
 * @return true if processed successfully
 */
bool ps4_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len);

/**
 * Get PS4 controller state
 * @param dev_addr USB device address
 * @return Pointer to controller state (NULL if not found)
 */
ps4_controller_t* ps4_get_controller(uint8_t dev_addr);

/**
 * Convert PS4 input to Atari joystick format
 * @param ps4 PS4 controller state
 * @param joystick_num Atari joystick number (0 or 1)
 * @param direction Output: direction byte (bit 0-3: up/down/left/right)
 * @param fire Output: fire button state (0 or 1)
 */
void ps4_to_atari(const ps4_controller_t* ps4, uint8_t joystick_num,
                  uint8_t* direction, uint8_t* fire);

/**
 * Return number of connected PS4 controllers
 */
uint8_t ps4_connected_count(void);

/**
 * Retrieve combined left/right stick axes for Llamatron mode
 * @param joy1_axis Output: standard left-stick/D-pad axis bits
 * @param joy1_fire Output: primary fire bit
 * @param joy0_axis Output: right-stick axis bits (no fire)
 * @return true if a controller was available
 */
bool ps4_llamatron_axes(uint8_t* joy1_axis, uint8_t* joy1_fire,
                        uint8_t* joy0_axis, uint8_t* joy0_fire);

/**
 * Set stick deadzone
 * @param dev_addr USB device address
 * @param deadzone Deadzone value (0-127, default 32)
 */
void ps4_set_deadzone(uint8_t dev_addr, int16_t deadzone);

/**
 * Mount callback
 * @param dev_addr USB device address
 */
void ps4_mount_cb(uint8_t dev_addr);

/**
 * Unmount callback
 * @param dev_addr USB device address
 */
void ps4_unmount_cb(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif /* PS4_CONTROLLER_H */

