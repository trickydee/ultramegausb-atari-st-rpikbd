/*
 * Atari ST RP2040 IKBD Emulator - PS5 DualSense Support
 * Copyright (C) 2025
 *
 * PS5 DualSense controller support for Atari ST joystick emulation.
 * USB report format: ID 0x01 (64 bytes) or ID 0x31 (78 bytes); payload at offset 1/2.
 * Based on Bluepad32 uni_hid_parser_ds5 and Amiga amigahid-pico ps5_controller.
 */

#ifndef PS5_CONTROLLER_H
#define PS5_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// PS5 DualSense Identification
//--------------------------------------------------------------------

#define PS5_VENDOR_ID          0x054C  // Sony (same as PS3/PS4)
#define PS5_DUALENSE_PID       0x0CE6  // DualSense (PS5)
#define PS5_DUALENSE_EDGE_PID  0x0DF2  // DualSense Edge

//--------------------------------------------------------------------
// PS5 DualSense Input Report (minimal, for joystick mapping)
// USB report ID 0x01: payload at offset 1 (x1,y1,x2,y2,rx,ry,rz, buttons0, buttons1).
// Report ID 0x31 (78 bytes): payload at offset 2, same layout.
//--------------------------------------------------------------------

#define PS5_REPORT_ID   0x31
#define PS5_USB_REPORT_ID 0x01
#define PS5_USB_MIN_LEN 10   // 1 (ID) + 9 (x1,y1,x2,y2,rx,ry,rz, buttons0, buttons1)

typedef struct TU_ATTR_PACKED {
    uint8_t x, y;       // Left stick (0-255, 127/128 = center)
    uint8_t rx, ry;     // Right stick
    uint8_t brake;      // L2 analog (0-255)
    uint8_t throttle;   // R2 analog (0-255)
    uint8_t buttons[2];  // buttons[0]: d-pad lo nibble, Square/Cross/Circle/Triangle; buttons[1]: L1 R1 L2 R2 Share Options L3 R3
} ps5_report_mini_t;

// D-pad values (buttons[0] & 0x0F)
#define PS5_DPAD_UP          0
#define PS5_DPAD_UP_RIGHT    1
#define PS5_DPAD_RIGHT       2
#define PS5_DPAD_DOWN_RIGHT  3
#define PS5_DPAD_DOWN        4
#define PS5_DPAD_DOWN_LEFT   5
#define PS5_DPAD_LEFT        6
#define PS5_DPAD_UP_LEFT     7
#define PS5_DPAD_CENTER      8   // or 0x0F when released

//--------------------------------------------------------------------
// PS5 Controller State
//--------------------------------------------------------------------

typedef struct {
    uint8_t dev_addr;
    bool connected;
    ps5_report_mini_t report;
    int16_t deadzone;
} ps5_controller_t;

//--------------------------------------------------------------------
// API Functions
//--------------------------------------------------------------------

bool ps5_is_dualsense(uint16_t vid, uint16_t pid);
bool ps5_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len);
ps5_controller_t* ps5_get_controller(uint8_t dev_addr);

/**
 * Convert PS5 input to Atari joystick format
 */
void ps5_to_atari(const ps5_controller_t* ps5, uint8_t joystick_num,
                  uint8_t* direction, uint8_t* fire);

uint8_t ps5_connected_count(void);

/**
 * Retrieve combined left/right stick axes for Llamatron mode
 */
bool ps5_llamatron_axes(uint8_t* joy1_axis, uint8_t* joy1_fire,
                        uint8_t* joy0_axis, uint8_t* joy0_fire);

void ps5_set_deadzone(uint8_t dev_addr, int16_t deadzone);
void ps5_mount_cb(uint8_t dev_addr);
void ps5_unmount_cb(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif /* PS5_CONTROLLER_H */
