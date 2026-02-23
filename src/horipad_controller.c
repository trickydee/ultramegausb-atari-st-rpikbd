/*
 * Atari ST RP2040 IKBD Emulator - HORI HORIPAD (Switch) Support
 * Copyright (C) 2025
 *
 * HORI HORIPAD for Nintendo Switch. Report: byte0 = y,b,a,x,l1,r1,l2,r2;
 * byte1 = s1,s2,l3,r3,a1,a2; byte2 = dpad:4; bytes 3-6 = axis_x,y,z,rz.
 * HID convention: 0=up/left, 128=center, 255=down/right.
 * Based on Amiga amigahid-pico horipad_controller.
 */

#include "horipad_controller.h"
#include "config.h"
#include "tusb.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>

#define MAX_HORIPAD_CONTROLLERS  2

static horipad_controller_t controllers[MAX_HORIPAD_CONTROLLERS];
static uint8_t controller_count = 0;

static horipad_controller_t* find_controller_by_addr(uint8_t dev_addr) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected)
            return &controllers[i];
    }
    return NULL;
}

static horipad_controller_t* allocate_controller(uint8_t dev_addr) {
    if (controller_count >= MAX_HORIPAD_CONTROLLERS) {
        printf("HORIPAD: Max controllers reached\n");
        return NULL;
    }
    horipad_controller_t* ctrl = &controllers[controller_count++];
    memset(ctrl, 0, sizeof(horipad_controller_t));
    ctrl->dev_addr = dev_addr;
    ctrl->connected = true;
    ctrl->deadzone = HORIPAD_DEADZONE;
    return ctrl;
}

static void free_controller(uint8_t dev_addr) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].dev_addr == dev_addr) {
            for (uint8_t j = i; j < controller_count - 1; j++)
                controllers[j] = controllers[j + 1];
            controller_count--;
            break;
        }
    }
}

bool horipad_is_controller(uint16_t vid, uint16_t pid) {
    return (vid == HORIPAD_VENDOR_ID && pid == HORIPAD_PID);
}

// Report layout: 3 bytes buttons+dpad, 4 bytes axes (x,y,z,rz)
void horipad_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len) {
    if (!report || len < 7) return;
    if (len >= 8 && (report[0] == 0x00 || report[0] == 0x01)) {
        report++;
        len--;
    }
    if (len < 7) return;

    horipad_controller_t* ctrl = find_controller_by_addr(dev_addr);
    if (!ctrl) {
        ctrl = allocate_controller(dev_addr);
        if (!ctrl) return;
    }

    uint8_t b0 = report[0], b2 = report[2];
    ctrl->y  = (b0 >> 0) & 1;
    ctrl->b  = (b0 >> 1) & 1;
    ctrl->a  = (b0 >> 2) & 1;
    ctrl->x  = (b0 >> 3) & 1;
    ctrl->l1 = (b0 >> 4) & 1;
    ctrl->r1 = (b0 >> 5) & 1;
    ctrl->l2 = (b0 >> 6) & 1;
    ctrl->r2 = (b0 >> 7) & 1;
    ctrl->dpad = b2 & 0x0F;
    ctrl->axis_x  = report[3];
    ctrl->axis_y  = report[4];
    ctrl->axis_z  = report[5];
    ctrl->axis_rz = report[6];
}

horipad_controller_t* horipad_get_controller(uint8_t dev_addr) {
    return find_controller_by_addr(dev_addr);
}

static void horipad_compute_axes(const horipad_controller_t* hp,
                                  uint8_t* left_axis, uint8_t* fire,
                                  uint8_t* right_axis, uint8_t* joy0_fire) {
    if (left_axis) {
        *left_axis = 0;
        if (hp->dpad < 8) {
            switch (hp->dpad) {
                case 0: *left_axis = 0x01; break;
                case 1: *left_axis = 0x09; break;
                case 2: *left_axis = 0x08; break;
                case 3: *left_axis = 0x0A; break;
                case 4: *left_axis = 0x02; break;
                case 5: *left_axis = 0x06; break;
                case 6: *left_axis = 0x04; break;
                case 7: *left_axis = 0x05; break;
                default: break;
            }
        } else {
            int16_t sx = (int16_t)hp->axis_x - 128;
            int16_t sy = (int16_t)hp->axis_y - 128;
            if (sy < -hp->deadzone) *left_axis |= 0x01;
            if (sy > hp->deadzone)  *left_axis |= 0x02;
            if (sx < -hp->deadzone) *left_axis |= 0x04;
            if (sx > hp->deadzone)  *left_axis |= 0x08;
        }
    }
    if (fire)
        *fire = (hp->b || hp->r2) ? 1 : 0;
    if (right_axis) {
        *right_axis = 0;
        int16_t rz = (int16_t)hp->axis_z - 128;
        int16_t rrz = (int16_t)hp->axis_rz - 128;
        if (rrz < -hp->deadzone) *right_axis |= 0x01;
        if (rrz > hp->deadzone)  *right_axis |= 0x02;
        if (rz < -hp->deadzone)  *right_axis |= 0x04;
        if (rz > hp->deadzone)   *right_axis |= 0x08;
    }
    if (joy0_fire)
        *joy0_fire = hp->a ? 1 : 0;
}

void horipad_to_atari(const horipad_controller_t* hp, uint8_t joystick_num,
                      uint8_t* direction, uint8_t* fire) {
    if (!hp || !direction || !fire) return;
    uint8_t right_dummy;
    horipad_compute_axes(hp, direction, fire, &right_dummy, NULL);
}

uint8_t horipad_connected_count(void) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < controller_count; i++)
        if (controllers[i].connected) n++;
    return n;
}

bool horipad_llamatron_axes(uint8_t* joy1_axis, uint8_t* joy1_fire,
                            uint8_t* joy0_axis, uint8_t* joy0_fire) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].connected) {
            horipad_compute_axes(&controllers[i], joy1_axis, joy1_fire, joy0_axis, joy0_fire);
            return true;
        }
    }
    return false;
}

void horipad_mount_cb(uint8_t dev_addr) {
#if ENABLE_SERIAL_LOGGING
    printf("HORIPAD: HORI HORIPAD (Switch) detected (addr=%d)\n", dev_addr);
#endif

#if ENABLE_OLED_DISPLAY
    extern ssd1306_t disp;
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 25, 10, 2, (char*)"HORI");
    ssd1306_draw_string(&disp, 5, 35, 1, (char*)"HORIPAD (Switch)");
    char line[20];
    snprintf(line, sizeof(line), "Addr:%d", dev_addr);
    ssd1306_draw_string(&disp, 25, 50, 1, line);
    ssd1306_show(&disp);
    sleep_ms(2000);
#endif

    if (!allocate_controller(dev_addr)) {
#if ENABLE_SERIAL_LOGGING
        printf("HORIPAD: Failed to allocate controller\n");
#endif
    }
}

void horipad_unmount_cb(uint8_t dev_addr) {
#if ENABLE_SERIAL_LOGGING
    printf("HORIPAD: Controller unmount (addr=%d)\n", dev_addr);
#endif
    free_controller(dev_addr);
}
