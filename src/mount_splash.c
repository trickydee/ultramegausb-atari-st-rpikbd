/*
 * Atari ST RP2040 IKBD Emulator
 * Non-blocking OLED splash for USB device mount.
 */

#include "mount_splash.h"
#include "version.h"

#if ENABLE_OLED_DISPLAY

#include "ssd1306.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

extern ssd1306_t disp;

static bool splash_pending = false;
static bool splash_active = false;
static bool splash_drawing = false;
static uint32_t splash_duration_ms = MOUNT_SPLASH_DEFAULT_MS;
static absolute_time_t splash_until;
static char splash_title[24];
static char splash_subtitle[32];
static char splash_detail[24];

void mount_splash_set_drawing(bool drawing) {
    splash_drawing = drawing;
}

bool mount_splash_blocks_oled(void) {
    return (splash_pending || splash_active) && !splash_drawing;
}

static void draw_centered(const char* text, int y, int scale) {
    if (!text || !text[0]) {
        return;
    }
    size_t len = strlen(text);
    if (len > 16) {
        len = 16;
    }
    char buf[17];
    memcpy(buf, text, len);
    buf[len] = '\0';
    const int char_width = 6 * scale;
    const int width = (int)len * char_width;
    int x = (SSD1306_WIDTH - width) / 2;
    if (x < 0) {
        x = 0;
    }
    ssd1306_draw_string(&disp, x, y, scale, buf);
}

static void draw_splash(void) {
    mount_splash_set_drawing(true);
    ssd1306_clear(&disp);
    draw_centered(splash_title, 4, 2);
    draw_centered(splash_subtitle, 28, 1);
    if (splash_detail[0]) {
        draw_centered(splash_detail, 42, 1);
    }
    draw_centered("v" PROJECT_VERSION_STRING, 54, 1);
    ssd1306_show(&disp);
    mount_splash_set_drawing(false);
}

static void copy_field(char* dst, size_t dst_len, const char* src) {
    if (!src || !src[0]) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

// absolute_time_diff_us(from, to) == to - from, so this is the time remaining
// until splash_until. The splash has expired once that remaining time hits zero.
static bool splash_expired(void) {
    return absolute_time_diff_us(get_absolute_time(), splash_until) <= 0;
}

static void activate_splash(void) {
    splash_active = true;
    splash_until = delayed_by_ms(get_absolute_time(), splash_duration_ms);
    draw_splash();
}

// Queue splash from a USB mount callback; draw happens in mount_splash_service().
void mount_splash_show(uint32_t duration_ms, const char* title, const char* subtitle, const char* detail) {
    if (!duration_ms) {
        duration_ms = MOUNT_SPLASH_DEFAULT_MS;
    }
    copy_field(splash_title, sizeof(splash_title), title);
    copy_field(splash_subtitle, sizeof(splash_subtitle), subtitle);
    copy_field(splash_detail, sizeof(splash_detail), detail);
    splash_duration_ms = duration_ms;
    splash_pending = true;
}

void mount_splash_service(void) {
    if (splash_pending) {
        splash_pending = false;
        activate_splash();
    }
    // While active the OLED holds the image; mount_splash_blocks_oled() suppresses
    // other writers. Do not redraw here — full-frame I2C every 2 ms starves input.
}

bool mount_splash_poll(void) {
    if (!splash_active) {
        return false;
    }

    if (splash_expired()) {
        splash_active = false;
        return true;
    }

    return false;
}

bool mount_splash_is_active(void) {
    return splash_pending || splash_active;
}

#endif
