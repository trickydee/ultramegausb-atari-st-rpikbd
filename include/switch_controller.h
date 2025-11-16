/*
 * Atari ST RP2040 IKBD Emulator - Nintendo Switch Controller Support
 * Copyright (C) 2025
 * 
 * Nintendo Switch controller implementation (Pro Controller, PowerA, etc.)
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef SWITCH_CONTROLLER_H
#define SWITCH_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// Nintendo Switch Controller Identification
//--------------------------------------------------------------------

// Official Nintendo Vendor ID
#define SWITCH_VENDOR_ID        0x057E

// Official Switch Controller Product IDs
#define SWITCH_PRO_CONTROLLER   0x2009  // Pro Controller
#define SWITCH_JOYCON_L         0x2006  // Joy-Con Left
#define SWITCH_JOYCON_R         0x2007  // Joy-Con Right
#define SWITCH_JOYCON_PAIR      0x2008  // Joy-Con Pair

// PowerA Vendor ID (third-party Switch controllers)
#define POWERA_VENDOR_ID        0x20D6

// PowerA Product IDs
#define POWERA_FUSION_ARCADE    0xA711  // Fusion Wireless Arcade Stick
#define POWERA_FUSION_ARCADE_V2 0xA715  // Fusion Wireless Arcade Stick (newer version)
#define POWERA_WIRED_PLUS       0xA712  // Wired Controller Plus (common)
#define POWERA_WIRELESS         0xA713  // Wireless Controller (common)

//--------------------------------------------------------------------
// Switch Controller State
//--------------------------------------------------------------------

#define MAX_SWITCH_CONTROLLERS  2

typedef struct {
    uint8_t dev_addr;           // USB device address
    uint8_t instance;           // Instance number
    bool    connected;          // Connection status
    
    // Parsed input state
    uint16_t buttons;           // Button bit field
    int16_t  stick_left_x;      // Left stick X (-128 to 127 typically)
    int16_t  stick_left_y;      // Left stick Y
    int16_t  stick_right_x;     // Right stick X
    int16_t  stick_right_y;     // Right stick Y
    uint8_t  dpad;              // D-Pad state (hat switch)
    
    // Configuration
    int16_t  deadzone;          // Stick deadzone (default 20)
    
} switch_controller_t;

//--------------------------------------------------------------------
// Switch Button Definitions (common across most Switch controllers)
//--------------------------------------------------------------------

#define SWITCH_BTN_Y            0x0001
#define SWITCH_BTN_B            0x0002
#define SWITCH_BTN_A            0x0004
#define SWITCH_BTN_X            0x0008
#define SWITCH_BTN_L            0x0010  // Left shoulder
#define SWITCH_BTN_R            0x0020  // Right shoulder
#define SWITCH_BTN_ZL           0x0040  // Left trigger
#define SWITCH_BTN_ZR           0x0080  // Right trigger
#define SWITCH_BTN_MINUS        0x0100
#define SWITCH_BTN_PLUS         0x0200
#define SWITCH_BTN_LSTICK       0x0400  // Left stick click
#define SWITCH_BTN_RSTICK       0x0800  // Right stick click
#define SWITCH_BTN_HOME         0x1000
#define SWITCH_BTN_CAPTURE      0x2000

// D-Pad values (hat switch)
#define SWITCH_DPAD_UP          0
#define SWITCH_DPAD_UP_RIGHT    1
#define SWITCH_DPAD_RIGHT       2
#define SWITCH_DPAD_DOWN_RIGHT  3
#define SWITCH_DPAD_DOWN        4
#define SWITCH_DPAD_DOWN_LEFT   5
#define SWITCH_DPAD_LEFT        6
#define SWITCH_DPAD_UP_LEFT     7
#define SWITCH_DPAD_NEUTRAL     15

//--------------------------------------------------------------------
// API Functions
//--------------------------------------------------------------------

/**
 * Check if a device is a Switch controller
 * @param vid Vendor ID
 * @param pid Product ID
 * @return true if device is Switch controller
 */
bool switch_is_controller(uint16_t vid, uint16_t pid);

/**
 * Get controller by device address
 * @param dev_addr USB device address
 * @return Pointer to controller (NULL if not found)
 */
switch_controller_t* switch_get_controller(uint8_t dev_addr);

/**
 * Process Switch controller HID report
 * @param dev_addr USB device address
 * @param report Pointer to HID report data
 * @param len Report length
 */
void switch_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len);

/**
 * Convert Switch input to Atari joystick format
 * @param sw Switch controller state
 * @param joystick_num Atari joystick number (0 or 1)
 * @param direction Output: direction byte
 * @param fire Output: fire button state
 */
void switch_to_atari(const switch_controller_t* sw, uint8_t joystick_num, 
                     uint8_t* direction, uint8_t* fire);

/**
 * Return number of connected Switch controllers
 */
uint8_t switch_connected_count(void);

/**
 * Retrieve dual-stick axes for Llamatron mode
 * @param joy1_axis Output: left stick/D-pad axis bits
 * @param joy1_fire Output: fire bit (B/A/ZR)
 * @param joy0_axis Output: right stick axis bits
 * @return true if controller data available
 */
bool switch_llamatron_axes(uint8_t* joy1_axis, uint8_t* joy1_fire,
                           uint8_t* joy0_axis, uint8_t* joy0_fire);

/**
 * Mount callback - called when Switch controller connected
 * @param dev_addr USB device address
 */
void switch_mount_cb(uint8_t dev_addr);

/**
 * Unmount callback - called when Switch controller disconnected
 * @param dev_addr USB device address
 */
void switch_unmount_cb(uint8_t dev_addr);

/**
 * Set stick deadzone
 * @param dev_addr USB device address
 * @param deadzone Deadzone value (0-127, default 20)
 */
void switch_set_deadzone(uint8_t dev_addr, int16_t deadzone);

/**
 * Get latest Switch controller values for OLED debug display
 * @param buttons Output: button bitfield
 * @param dpad Output: D-pad value
 * @param lx Output: left stick X
 * @param ly Output: left stick Y
 * @param atari_dir Output: Atari direction sent
 * @param atari_fire Output: Atari fire button sent
 */
void switch_get_debug_values(uint16_t* buttons, uint8_t* dpad, int16_t* lx, int16_t* ly,
                              uint8_t* atari_dir, uint8_t* atari_fire);

/**
 * Get report count for debugging
 * @return Number of reports received
 */
uint32_t switch_get_report_count(void);

/**
 * Get Pro Controller initialization status
 * @param attempted Output: true if init was attempted
 * @param complete Output: true if init completed
 * @param len_before Output: report length before init
 * @param len_after Output: report length after init
 */
void switch_get_pro_init_status(bool* attempted, bool* complete, uint16_t* len_before, uint16_t* len_after);

/**
 * Get elapsed time since Pro Controller mount (for debugging)
 * @return Milliseconds elapsed, or 0 if not mounted
 */
uint32_t switch_get_pro_init_elapsed(void);

/**
 * Check if Pro Controller init is scheduled
 * @return true if waiting to init
 */
bool switch_get_pro_init_scheduled(void);

/**
 * Get raw report bytes for debugging
 * @param bytes Output: bytes 3-11 from last report (9 bytes)
 * @param len Output: last report length
 */
void switch_get_raw_bytes(uint8_t* bytes, uint16_t* len);

/**
 * Get init command success bitmask (for debugging)
 * @return Bitmask: bits 0-6 for each of 7 commands (0x7F = all success)
 */
uint8_t switch_get_init_cmd_success(void);

/**
 * Initialize Pro Controller (send USB handshake)
 * @param dev_addr USB device address
 * @return true if initialization successful
 */
bool switch_init_pro_controller(uint8_t dev_addr);

/**
 * Check and perform delayed Pro Controller initialization
 * Call this periodically from main loop
 */
void switch_check_delayed_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SWITCH_CONTROLLER_H */

