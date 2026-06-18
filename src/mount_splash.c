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

static void draw_splash(const char* title, const char* subtitle, const char* detail) {
    ssd1306_clear(&disp);
    if (title && title[0]) {
        ssd1306_draw_string(&disp, 0, 10, 2, (char*)title);
    }
    if (subtitle && subtitle[0]) {
        ssd1306_draw_string(&disp, 0, 35, 1, (char*)subtitle);
    }
    if (detail && detail[0]) {
        ssd1306_draw_string(&disp, 0, 50, 1, (char*)detail);
    }
    ssd1306_show(&disp);
}

void mount_splash_show(uint32_t duration_ms, const char* title, const char* subtitle, const char* detail) {
    if (!duration_ms) {
        duration_ms = MOUNT_SPLASH_DEFAULT_MS;
    }
    draw_splash(title, subtitle, detail);
    splash_active = true;
    splash_until = delayed_by_ms(get_absolute_time(), duration_ms);
}

void mount_splash_poll(void) {
    if (!splash_active) {
        return;
    }
    if (absolute_time_diff_us(get_absolute_time(), splash_until) >= 0) {
        splash_active = false;
    }
}

bool mount_splash_is_active(void) {
    mount_splash_poll();
    return splash_active;
}

#endif
