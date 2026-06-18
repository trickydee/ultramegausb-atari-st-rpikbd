/*
 * Atari ST RP2040 IKBD Emulator
 * Non-blocking OLED splash for USB device mount (avoids sleep_ms in callbacks).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MOUNT_SPLASH_DEFAULT_MS 2000

#ifdef __cplusplus
extern "C" {
#endif

#if ENABLE_OLED_DISPLAY
void mount_splash_show(uint32_t duration_ms, const char* title, const char* subtitle, const char* detail);
void mount_splash_poll(void);
bool mount_splash_is_active(void);
#else
static inline void mount_splash_show(uint32_t duration_ms, const char* title, const char* subtitle, const char* detail) {
    (void)duration_ms;
    (void)title;
    (void)subtitle;
    (void)detail;
}
static inline void mount_splash_poll(void) {}
static inline bool mount_splash_is_active(void) { return false; }
#endif

#ifdef __cplusplus
}
#endif
