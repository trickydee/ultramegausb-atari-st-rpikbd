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

// Get keyboard data for a specific index (0-1)
// Returns true if keyboard is connected and has data
// out_keyboard must point to a struct matching uni_keyboard_t layout
// Marks data as read (clears updated flag)
bool bluepad32_get_keyboard(int idx, void* out_keyboard);

// Peek at keyboard data without marking as read (for shortcuts)
// Returns true if keyboard is connected
// out_keyboard must point to a struct matching uni_keyboard_t layout
bool bluepad32_peek_keyboard(int idx, void* out_keyboard);

// Get mouse data for a specific index (0-1)
// Returns true if mouse is connected and has data
// out_mouse must point to a struct matching uni_mouse_t layout
bool bluepad32_get_mouse(int idx, void* out_mouse);

// Get count of connected Bluetooth keyboards
int bluepad32_get_keyboard_count(void);

// Get count of connected Bluetooth mice
int bluepad32_get_mouse_count(void);

#ifdef __cplusplus
}
#endif

#endif // BLUEPAD32_PLATFORM_H

