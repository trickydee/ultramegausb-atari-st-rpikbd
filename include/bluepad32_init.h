/*
 * Bluepad32 Initialization Header
 * Separated from main.cpp to avoid HID type conflicts
 */

#ifndef BLUEPAD32_INIT_H
#define BLUEPAD32_INIT_H

#include <pico/async_context_poll.h>

#ifdef ENABLE_BLUEPAD32

#ifdef __cplusplus
extern "C" {
#endif

// Initialize Bluepad32 and return the async context (or NULL on failure)
async_context_poll_t* bluepad32_init(void);

// Poll btstack async_context (non-blocking, call from main loop)
void bluepad32_poll(void);

#ifdef __cplusplus
}
#endif

#endif // ENABLE_BLUEPAD32

#endif // BLUEPAD32_INIT_H

