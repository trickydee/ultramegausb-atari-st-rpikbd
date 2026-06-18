/*
 * Atari ST RP2040 IKBD Emulator
 * Non-blocking OLED splash for USB device mount (avoids sleep_ms in callbacks).
 */
#pragma once

#include "config.h"
#include <stdint.h>
#include <stdbool.h>

#define MOUNT_SPLASH_DEFAULT_MS 5000

#ifdef __cplusplus
extern "C" {
#endif

#if ENABLE_OLED_DISPLAY
void mount_splash_show(uint32_t duration_ms, const char* title, const char* subtitle, const char* detail);
/** Activate pending splash and refresh; call after tuh_task() in the main loop. */
void mount_splash_service(void);
/** Returns true when the splash just expired (caller should refresh the UI). */
bool mount_splash_poll(void);
bool mount_splash_is_active(void);
/** True while mount splash owns the OLED (suppress other writers). */
bool mount_splash_blocks_oled(void);
void mount_splash_set_drawing(bool drawing);
#else
static inline void mount_splash_show(uint32_t duration_ms, const char* title, const char* subtitle, const char* detail) {
    (void)duration_ms;
    (void)title;
    (void)subtitle;
    (void)detail;
}
static inline void mount_splash_service(void) {}
static inline bool mount_splash_poll(void) { return false; }
static inline bool mount_splash_is_active(void) { return false; }
static inline bool mount_splash_blocks_oled(void) { return false; }
static inline void mount_splash_set_drawing(bool drawing) { (void)drawing; }
#endif

#ifdef __cplusplus
}
#endif
