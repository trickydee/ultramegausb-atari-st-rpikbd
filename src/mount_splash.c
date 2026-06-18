/*
 * Atari ST RP2040 IKBD Emulator
 * Non-blocking OLED splash for USB device mount.
 */

#include "mount_splash.h"
#include "config.h"

#if ENABLE_OLED_DISPLAY

#include "ssd1306.h"
#include "pico/stdlib.h"
#include <string.h>

extern ssd1306_t disp;

static bool splash_active = false;
static absolute_time_t splash_until;
static char splash_title[24];
static char splash_subtitle[32];
static char splash_detail[24];

static void draw_splash(void) {
    ssd1306_clear(&disp);
    if (splash_title[0]) {
        ssd1306_draw_string(&disp, 0, 10, 2, splash_title);
    }
    if (splash_subtitle[0]) {
        ssd1306_draw_string(&disp, 0, 35, 1, splash_subtitle);
    }
    if (splash_detail[0]) {
        ssd1306_draw_string(&disp, 0, 50, 1, splash_detail);
    }
    ssd1306_show(&disp);
}

static void copy_field(char* dst, size_t dst_len, const char* src) {
    if (!src || !src[0]) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

void mount_splash_show(uint32_t duration_ms, const char* title, const char* subtitle, const char* detail) {
    if (!duration_ms) {
        duration_ms = MOUNT_SPLASH_DEFAULT_MS;
    }
    copy_field(splash_title, sizeof(splash_title), title);
    copy_field(splash_subtitle, sizeof(splash_subtitle), subtitle);
    copy_field(splash_detail, sizeof(splash_detail), detail);
    splash_active = true;
    splash_until = delayed_by_ms(get_absolute_time(), duration_ms);
    draw_splash();
}

void mount_splash_poll(void) {
    if (!splash_active) {
        return;
    }
    if (absolute_time_diff_us(get_absolute_time(), splash_until) >= 0) {
        splash_active = false;
        return;
    }
    // Re-draw each poll so a deferred UI update cannot flash over the splash.
    draw_splash();
}

bool mount_splash_is_active(void) {
    return splash_active;
}

#endif
