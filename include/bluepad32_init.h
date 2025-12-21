/*
 * Bluepad32 Initialization Header
 * Separated from main.cpp to avoid HID type conflicts
 */

#ifndef BLUEPAD32_INIT_H
#define BLUEPAD32_INIT_H

// Only include async_context_poll when Bluetooth support is enabled.
// ENABLE_BLUEPAD32 is defined as 0 or 1 via CMake.
#if ENABLE_BLUEPAD32

#include <pico/async_context_poll.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize Bluepad32 and return the async context (or NULL on failure)
async_context_poll_t* bluepad32_init(void);

// Poll btstack async_context (non-blocking, call from main loop)
void bluepad32_poll(void);

// Check if UI update is needed and perform it (called from main loop)
// This defers UI updates from Bluetooth callbacks to prevent blocking
void bluepad32_check_ui_update(void);

// Runtime control functions
void bluepad32_enable(void);
void bluepad32_disable(void);
bool bluepad32_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif // ENABLE_BLUEPAD32

#endif // BLUEPAD32_INIT_H

