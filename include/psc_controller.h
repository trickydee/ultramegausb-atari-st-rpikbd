/*
 * Atari ST RP2040 IKBD Emulator - PlayStation Classic (PSC) Support
 * Copyright (C) 2025
 *
 * Sony PlayStation Classic controller support for Atari ST joystick emulation.
 * Report format: 3-byte HID report (buttons, d-pad); no analog sticks.
 * Based on Amiga amigahid-pico psc_controller and joypad-os sony_psc.
 */

#ifndef PSC_CONTROLLER_H
#define PSC_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSC_VENDOR_ID  0x054C
#define PSC_PID        0x0CDA  // Sony PlayStation Classic

typedef struct {
    uint8_t dev_addr;
    bool connected;
    uint8_t dpad;       // HID hat 0-7, 8=released
    uint8_t cross, circle, square, triangle;
    uint8_t l1, r1, l2, r2;
} psc_controller_t;

bool psc_is_controller(uint16_t vid, uint16_t pid);
void psc_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len);
psc_controller_t* psc_get_controller(uint8_t dev_addr);

void psc_to_atari(const psc_controller_t* psc, uint8_t joystick_num,
                  uint8_t* direction, uint8_t* fire);

uint8_t psc_connected_count(void);
void psc_mount_cb(uint8_t dev_addr);
void psc_unmount_cb(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif /* PSC_CONTROLLER_H */
