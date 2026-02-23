/*
 * Atari ST RP2040 IKBD Emulator - PlayStation Classic (PSC) Support
 * Copyright (C) 2025
 *
 * Sony PlayStation Classic controller (0x054C / 0x0CDA).
 * Report format: 3 bytes - byte0 = triangle,circle,cross,square,l2,r2,l1,r1;
 *                byte1 = share,option, d-pad(4 bits); byte2 = counter.
 * Based on Amiga amigahid-pico psc_controller.
 */

#include "psc_controller.h"
#include "config.h"
#include "tusb.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>

#define MAX_PSC_CONTROLLERS  2

static psc_controller_t controllers[MAX_PSC_CONTROLLERS];
static uint8_t controller_count = 0;

static psc_controller_t* find_controller_by_addr(uint8_t dev_addr) {
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected)
            return &controllers[i];
    }
    return NULL;
}

static psc_controller_t* allocate_controller(uint8_t dev_addr) {
    if (controller_count >= MAX_PSC_CONTROLLERS) {
        printf("PSC: Max controllers reached\n");
        return NULL;
    }
    psc_controller_t* ctrl = &controllers[controller_count++];
    memset(ctrl, 0, sizeof(psc_controller_t));
    ctrl->dev_addr = dev_addr;
    ctrl->connected = true;
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

bool psc_is_controller(uint16_t vid, uint16_t pid) {
    return (vid == PSC_VENDOR_ID && pid == PSC_PID);
}

// PSC report: byte0 = triangle,circle,cross,square,l2,r2,l1,r1 (bit0=r1, bit7=triangle)
//             byte1 = share,option, d-pad(4 bits); byte2 = counter
void psc_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len) {
    if (!report || len < 3) return;
    if (len >= 4 && (report[0] == 0x00 || report[0] == 0x01)) {
        report++;
        len--;
    }
    if (len < 3) return;

    psc_controller_t* ctrl = find_controller_by_addr(dev_addr);
    if (!ctrl) {
        ctrl = allocate_controller(dev_addr);
        if (!ctrl) return;
    }

    uint8_t b0 = report[0], b1 = report[1];
    ctrl->triangle = (b0 >> 7) & 1;
    ctrl->circle   = (b0 >> 6) & 1;
    ctrl->cross    = (b0 >> 5) & 1;
    ctrl->square   = (b0 >> 4) & 1;
    ctrl->l2 = (b0 >> 3) & 1;
    ctrl->r2 = (b0 >> 2) & 1;
    ctrl->l1 = (b0 >> 1) & 1;
    ctrl->r1 = (b0 >> 0) & 1;
    ctrl->dpad = b1 & 0x0F;
}

psc_controller_t* psc_get_controller(uint8_t dev_addr) {
    return find_controller_by_addr(dev_addr);
}

void psc_to_atari(const psc_controller_t* psc, uint8_t joystick_num,
                  uint8_t* direction, uint8_t* fire) {
    if (!psc || !direction || !fire) return;
    *direction = 0;
    if (psc->dpad < 8) {
        switch (psc->dpad) {
            case 0: *direction = 0x01; break;
            case 1: *direction = 0x09; break;
            case 2: *direction = 0x08; break;
            case 3: *direction = 0x0A; break;
            case 4: *direction = 0x02; break;
            case 5: *direction = 0x06; break;
            case 6: *direction = 0x04; break;
            case 7: *direction = 0x05; break;
            default: break;
        }
    }
    *fire = (psc->cross || psc->r2) ? 1 : 0;
}

uint8_t psc_connected_count(void) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < controller_count; i++)
        if (controllers[i].connected) n++;
    return n;
}

void psc_mount_cb(uint8_t dev_addr) {
#if ENABLE_SERIAL_LOGGING
    printf("PSC: PlayStation Classic controller detected (addr=%d)\n", dev_addr);
#endif

#if ENABLE_OLED_DISPLAY
    extern ssd1306_t disp;
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 15, 10, 2, (char*)"PSC");
    ssd1306_draw_string(&disp, 0, 35, 1, (char*)"PlayStation Classic");
    char line[20];
    snprintf(line, sizeof(line), "Addr:%d", dev_addr);
    ssd1306_draw_string(&disp, 25, 50, 1, line);
    ssd1306_show(&disp);
    sleep_ms(2000);
#endif

    if (!allocate_controller(dev_addr)) {
#if ENABLE_SERIAL_LOGGING
        printf("PSC: Failed to allocate controller\n");
#endif
    }
}

void psc_unmount_cb(uint8_t dev_addr) {
#if ENABLE_SERIAL_LOGGING
    printf("PSC: Controller unmount (addr=%d)\n", dev_addr);
#endif
    free_controller(dev_addr);
}
