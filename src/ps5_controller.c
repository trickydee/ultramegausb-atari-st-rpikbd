/*
 * Atari ST RP2040 IKBD Emulator - PS5 DualSense Support
 * Copyright (C) 2025
 *
 * PS5 DualSense controller implementation (USB HID).
 * Report format: ID 0x01 (USB, 64 bytes) or ID 0x31 (78 bytes); payload at offset 1 or 2.
 * Based on Amiga amigahid-pico ps5_controller and Bluepad32 uni_hid_parser_ds5.
 */

#include "ps5_controller.h"
#include "config.h"
#include "tusb.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>

#define MAX_PS5_CONTROLLERS  2

static ps5_controller_t controllers[MAX_PS5_CONTROLLERS];
static uint8_t controller_count = 0;

static ps5_controller_t* find_controller_by_addr(uint8_t dev_addr) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected) {
            return &controllers[i];
        }
    }
    return NULL;
}

static ps5_controller_t* allocate_controller(uint8_t dev_addr) {
    if (controller_count >= MAX_PS5_CONTROLLERS) {
        printf("PS5: Max controllers reached\n");
        return NULL;
    }
    ps5_controller_t* ctrl = &controllers[controller_count++];
    memset(ctrl, 0, sizeof(ps5_controller_t));
    ctrl->dev_addr = dev_addr;
    ctrl->connected = true;
    ctrl->deadzone = 20;
    return ctrl;
}

static void free_controller(uint8_t dev_addr) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].dev_addr == dev_addr) {
            for (uint8_t j = i; j < controller_count - 1; j++) {
                controllers[j] = controllers[j + 1];
            }
            controller_count--;
            break;
        }
    }
}

bool ps5_is_dualsense(uint16_t vid, uint16_t pid) {
    if (vid != PS5_VENDOR_ID) return false;
    return (pid == PS5_DUALENSE_PID || pid == PS5_DUALENSE_EDGE_PID);
}

// USB report: ID 0x01, payload at offset 1 (x1,y1,x2,y2,rx,ry,rz, buttons0, buttons1)
static void ps5_parse_usb_report(ps5_report_mini_t* input, const uint8_t* payload) {
    input->x        = payload[0];
    input->y        = payload[1];
    input->rx       = payload[2];
    input->ry       = payload[3];
    input->brake    = payload[4];
    input->throttle = payload[5];
    input->buttons[0] = payload[7];
    input->buttons[1] = payload[8];
}

// Report ID 0x31 (78 bytes): payload at offset 2, same layout
static void ps5_parse_bt_report(ps5_report_mini_t* input, const uint8_t* payload, uint16_t payload_len) {
    if (payload_len < 9) return;
    input->x        = payload[0];
    input->y        = payload[1];
    input->rx       = payload[2];
    input->ry       = payload[3];
    input->brake    = payload[4];
    input->throttle = payload[5];
    input->buttons[0] = payload[7];
    input->buttons[1] = payload[8];
}

bool ps5_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len) {
    ps5_controller_t* ctrl = find_controller_by_addr(dev_addr);
    if (!ctrl) {
        ctrl = allocate_controller(dev_addr);
        if (!ctrl) return false;
    }

    ps5_report_mini_t* input = &ctrl->report;

    if (len >= PS5_USB_MIN_LEN && report[0] == PS5_USB_REPORT_ID) {
        ps5_parse_usb_report(input, report + 1);
    } else if (len >= 2 + 9 && report[0] == PS5_REPORT_ID) {
        ps5_parse_bt_report(input, report + 2, len - 2);
    } else {
        return false;
    }
    return true;
}

ps5_controller_t* ps5_get_controller(uint8_t dev_addr) {
    return find_controller_by_addr(dev_addr);
}

// Compute direction (4-bit) and fire from report; optional right axis and joy0_fire for Llamatron
static void ps5_compute_axes(const ps5_controller_t* ps5,
                             uint8_t* left_axis, uint8_t* fire,
                             uint8_t* right_axis, uint8_t* joy0_fire) {
    const ps5_report_mini_t* input = &ps5->report;

    if (left_axis) {
        *left_axis = 0;
        uint8_t dpad = input->buttons[0] & 0x0F;
        if (dpad != PS5_DPAD_CENTER && dpad < 8) {
            switch (dpad) {
                case PS5_DPAD_UP:         *left_axis = 0x01; break;
                case PS5_DPAD_UP_RIGHT:   *left_axis = 0x09; break;
                case PS5_DPAD_RIGHT:     *left_axis = 0x08; break;
                case PS5_DPAD_DOWN_RIGHT:*left_axis = 0x0A; break;
                case PS5_DPAD_DOWN:      *left_axis = 0x02; break;
                case PS5_DPAD_DOWN_LEFT: *left_axis = 0x06; break;
                case PS5_DPAD_LEFT:      *left_axis = 0x04; break;
                case PS5_DPAD_UP_LEFT:   *left_axis = 0x05; break;
                default: break;
            }
        } else {
            int16_t stick_x = (int16_t)input->x - 127;
            int16_t stick_y = (int16_t)input->y - 127;
            if (stick_x < -ps5->deadzone || stick_x > ps5->deadzone ||
                stick_y < -ps5->deadzone || stick_y > ps5->deadzone) {
                if (stick_y < -ps5->deadzone) *left_axis |= 0x01;
                if (stick_y > ps5->deadzone)  *left_axis |= 0x02;
                if (stick_x < -ps5->deadzone) *left_axis |= 0x04;
                if (stick_x > ps5->deadzone)  *left_axis |= 0x08;
            }
        }
    }

    if (fire) {
        bool cross = (input->buttons[0] & 0x20) != 0;
        bool r2_digital = (input->buttons[1] & 0x08) != 0;
        *fire = (cross || input->throttle > 200 || r2_digital) ? 1 : 0;
    }

    if (right_axis) {
        *right_axis = 0;
        int16_t rx = (int16_t)input->rx - 127;
        int16_t ry = (int16_t)input->ry - 127;
        if (ry < -ps5->deadzone) *right_axis |= 0x01;
        if (ry > ps5->deadzone)  *right_axis |= 0x02;
        if (rx < -ps5->deadzone) *right_axis |= 0x04;
        if (rx > ps5->deadzone)  *right_axis |= 0x08;
    }

    if (joy0_fire) {
        *joy0_fire = (input->buttons[0] & 0x40) ? 1 : 0;  // Circle
    }
}

void ps5_to_atari(const ps5_controller_t* ps5, uint8_t joystick_num,
                  uint8_t* direction, uint8_t* fire) {
    if (!ps5 || !direction || !fire) return;
    uint8_t right_dummy;
    ps5_compute_axes(ps5, direction, fire, &right_dummy, NULL);
}

void ps5_set_deadzone(uint8_t dev_addr, int16_t deadzone) {
    ps5_controller_t* ctrl = find_controller_by_addr(dev_addr);
    if (ctrl) ctrl->deadzone = deadzone;
}

uint8_t ps5_connected_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].connected) count++;
    }
    return count;
}

bool ps5_llamatron_axes(uint8_t* joy1_axis, uint8_t* joy1_fire,
                        uint8_t* joy0_axis, uint8_t* joy0_fire) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].connected) {
            ps5_compute_axes(&controllers[i], joy1_axis, joy1_fire, joy0_axis, joy0_fire);
            return true;
        }
    }
    return false;
}

void ps5_mount_cb(uint8_t dev_addr) {
#if ENABLE_SERIAL_LOGGING
    printf("PS5: DualSense controller detected (addr=%d)\n", dev_addr);
#endif

#if ENABLE_OLED_DISPLAY
    extern ssd1306_t disp;
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 25, 10, 2, (char*)"PS5");
    ssd1306_draw_string(&disp, 5, 35, 1, (char*)"DualSense");
    char line[20];
    snprintf(line, sizeof(line), "Addr:%d", dev_addr);
    ssd1306_draw_string(&disp, 25, 50, 1, line);
    ssd1306_show(&disp);
    sleep_ms(2000);
#endif

    ps5_controller_t* ctrl = allocate_controller(dev_addr);
    if (ctrl) {
#if ENABLE_SERIAL_LOGGING
        printf("PS5: Controller registered\n");
#endif
    }
}

void ps5_unmount_cb(uint8_t dev_addr) {
#if ENABLE_SERIAL_LOGGING
    printf("PS5: Controller unmounted at address %d\n", dev_addr);
#endif
    free_controller(dev_addr);
}
