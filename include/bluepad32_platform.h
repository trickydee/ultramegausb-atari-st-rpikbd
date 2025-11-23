/*
 * Bluepad32 Platform Header
 * Public API for accessing Bluetooth gamepad data
 */

#ifndef BLUEPAD32_PLATFORM_H
#define BLUEPAD32_PLATFORM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration to avoid including uni.h (which causes HID type conflicts with TinyUSB)
// The actual type is uni_gamepad_t, but we use void* in the API to avoid conflicts
// Get gamepad data for a specific index (0-3)
// Returns true if gamepad is connected and has data
// out_gamepad must point to a struct matching uni_gamepad_t layout
bool bluepad32_get_gamepad(int idx, void* out_gamepad);

// Get count of connected Bluetooth gamepads
int bluepad32_get_connected_count(void);

#ifdef __cplusplus
}
#endif

#endif // BLUEPAD32_PLATFORM_H

