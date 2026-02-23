/*
 * Atari ST RP2040 IKBD Emulator - HORI HORIPAD (Switch) Support
 * Copyright (C) 2025
 *
 * HORI HORIPAD for Nintendo Switch (0x0F0D / 0x00C1).
 * Report format: buttons + d-pad + 4 axes (joypad-os hori_horipad_report_t).
 * Based on Amiga amigahid-pico horipad_controller.
 */

#ifndef HORIPAD_CONTROLLER_H
#define HORIPAD_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HORIPAD_VENDOR_ID  0x0F0D
#define HORIPAD_PID        0x00C1  // HORI HORIPAD (Switch)

#define HORIPAD_DEADZONE  20

typedef struct {
    uint8_t dev_addr;
    bool connected;
    uint8_t dpad;
    uint8_t axis_x, axis_y, axis_z, axis_rz;
    uint8_t b, a, y, x, l1, r1, l2, r2;
    int16_t deadzone;
} horipad_controller_t;

bool horipad_is_controller(uint16_t vid, uint16_t pid);
void horipad_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len);
horipad_controller_t* horipad_get_controller(uint8_t dev_addr);

void horipad_to_atari(const horipad_controller_t* hp, uint8_t joystick_num,
                      uint8_t* direction, uint8_t* fire);

uint8_t horipad_connected_count(void);
bool horipad_llamatron_axes(uint8_t* joy1_axis, uint8_t* joy1_fire,
                            uint8_t* joy0_axis, uint8_t* joy0_fire);

void horipad_mount_cb(uint8_t dev_addr);
void horipad_unmount_cb(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif /* HORIPAD_CONTROLLER_H */
