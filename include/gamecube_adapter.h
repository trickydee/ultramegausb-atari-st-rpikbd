/*
 * Atari ST RP2040 IKBD Emulator - GameCube Controller USB Adapter Support
 * Copyright (C) 2025
 * 
 * GameCube controller USB adapter support for Atari ST joystick emulation
 * Supports official Nintendo GameCube adapter and compatible third-party adapters in PC mode
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef GAMECUBE_ADAPTER_H
#define GAMECUBE_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// GameCube USB Adapter Identification
//--------------------------------------------------------------------

// Nintendo Vendor ID (same as Switch)
#define GAMECUBE_VENDOR_ID          0x057E

// GameCube Controller Adapter Product IDs
#define GAMECUBE_ADAPTER_PID        0x0337  // Official Nintendo adapter

//--------------------------------------------------------------------
// GameCube Controller Button Definitions
// Based on reference driver and testing
//--------------------------------------------------------------------

// Report format: 37 bytes total
// Byte 0: 0x21 (signal byte)
// Bytes 1-9: Port 1 controller
// Bytes 10-18: Port 2 controller
// Bytes 19-27: Port 3 controller  
// Bytes 28-36: Port 4 controller

// Each controller: 9 bytes
// Byte 0: Status (powered, type)
// Byte 1: Buttons 1 (D-Pad + face buttons)
// Byte 2: Buttons 2 (L, R, Z, START)
// Bytes 3-4: Analog Stick X, Y (0-255, 127=center)
// Bytes 5-6: C-Stick X, Y
// Bytes 7-8: L trigger, R trigger (analog)

// Button bitmasks (Byte 1)
#define GC_BTN_A         0x01
#define GC_BTN_B         0x02
#define GC_BTN_X         0x04
#define GC_BTN_Y         0x08
#define GC_BTN_DPAD_LEFT  0x10
#define GC_BTN_DPAD_RIGHT 0x20
#define GC_BTN_DPAD_DOWN  0x40
#define GC_BTN_DPAD_UP    0x80

// Button bitmasks (Byte 2)
#define GC_BTN_START     0x01
#define GC_BTN_Z         0x02
#define GC_BTN_R         0x04
#define GC_BTN_L         0x08

//--------------------------------------------------------------------
// GameCube Controller Input Structure (per controller)
//--------------------------------------------------------------------

typedef struct {
    // Status byte
    uint8_t powered : 1;    // Controller is powered/connected
    uint8_t : 1;
    uint8_t type : 2;       // Controller type (normal, wavebird, etc)
    uint8_t : 4;
    
    // Button bytes
    uint8_t buttons1;       // D-Pad + face buttons
    uint8_t buttons2;       // L, R, Z, START
    
    // Analog sticks (0-255, 127=center)
    uint8_t stick_x;
    uint8_t stick_y;
    uint8_t c_stick_x;
    uint8_t c_stick_y;
    
    // Analog triggers (0-255)
    uint8_t l_trigger;
    uint8_t r_trigger;
} gc_controller_input_t;

//--------------------------------------------------------------------
// GameCube Adapter Report (all 4 ports)
//--------------------------------------------------------------------

typedef struct {
    uint8_t signal;         // 0x21
    gc_controller_input_t port[4];
} gc_adapter_report_t;

//--------------------------------------------------------------------
// GameCube Adapter State
//--------------------------------------------------------------------

typedef struct {
    uint8_t dev_addr;           // USB device address
    bool connected;             // Adapter connection status
    gc_adapter_report_t report; // Latest report (all 4 ports)
    int16_t deadzone;           // Stick deadzone
    uint8_t active_port;        // Which port has a controller (0-3, or 0xFF if none)
} gc_adapter_t;

//--------------------------------------------------------------------
// API Functions
//--------------------------------------------------------------------

/**
 * Check if a device is a GameCube controller USB adapter
 * @param vid Vendor ID
 * @param pid Product ID
 * @return true if device is GameCube adapter
 */
bool gc_is_adapter(uint16_t vid, uint16_t pid);

/**
 * Process GameCube adapter report
 * @param dev_addr USB device address
 * @param report Pointer to HID report data
 * @param len Report length
 * @return true if processed successfully
 */
bool gc_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len);

/**
 * Get GameCube adapter state
 * @param dev_addr USB device address
 * @return Pointer to adapter state (NULL if not found)
 */
gc_adapter_t* gc_get_adapter(uint8_t dev_addr);

/**
 * Convert GameCube input to Atari joystick format
 * @param gc GameCube adapter state
 * @param joystick_num Atari joystick number (0 or 1)
 * @param direction Output: direction byte (bit 0-3: up/down/left/right)
 * @param fire Output: fire button state (0 or 1)
 */
void gc_to_atari(const gc_adapter_t* gc, uint8_t joystick_num,
                 uint8_t* direction, uint8_t* fire);

/**
 * Set stick deadzone
 * @param dev_addr USB device address
 * @param deadzone Deadzone value (0-127, default 35)
 */
void gc_set_deadzone(uint8_t dev_addr, int16_t deadzone);

/**
 * Mount callback
 * @param dev_addr USB device address
 */
void gc_mount_cb(uint8_t dev_addr);

/**
 * Unmount callback
 * @param dev_addr USB device address
 */
void gc_unmount_cb(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif /* GAMECUBE_ADAPTER_H */

