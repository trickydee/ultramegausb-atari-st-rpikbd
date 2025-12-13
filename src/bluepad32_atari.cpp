/*
 * Bluepad32 to Atari ST Joystick Converter
 * Converts Bluepad32 gamepad format to Atari ST joystick format
 */

#include <stdint.h>
#include <stdbool.h>
#include "bluepad32_platform.h"
#include <uni.h>

#ifdef __cplusplus
extern "C" {
#endif

// Convert Bluepad32 gamepad to Atari ST joystick format
// Similar to xinput_to_atari_joystick, ps4_to_atari, etc.
// Uses void* to avoid exposing uni_gamepad_t in header (prevents HID type conflicts)
bool bluepad32_to_atari_joystick(const void* gp_ptr, uint8_t* axis, uint8_t* button) {
    // Cast to uni_gamepad_t (we know the layout matches)
    const uni_gamepad_t* gp = (const uni_gamepad_t*)gp_ptr;
    if (!gp || !axis || !button) {
        return false;
    }

    *axis = 0;
    *button = 0;

    // Dead zone for analog sticks (similar to other controllers)
    // Bluepad32 normalizes axes to -32768 to 32767 range (like XInput)
    // Use ~25% deadzone (8000) for normalized range, similar to Xbox controllers
    const int32_t DEAD_ZONE = 8000;  // ~25% of -32768 to 32767 range
    
    // However, if we see very small values (like 2, 2), the controller might be sending
    // raw values or the axis might not be properly initialized
    // Let's use a smaller deadzone for very small values
    int32_t deadzone = DEAD_ZONE;
    if (abs(gp->axis_x) < 100 && abs(gp->axis_y) < 100) {
        // Very small values - use tiny deadzone (might be raw values or noise)
        deadzone = 10;
    }

    // D-Pad has priority (like other controllers)
    if (gp->dpad & DPAD_UP)    *axis |= 0x01;
    if (gp->dpad & DPAD_DOWN)  *axis |= 0x02;
    if (gp->dpad & DPAD_LEFT)  *axis |= 0x04;
    if (gp->dpad & DPAD_RIGHT) *axis |= 0x08;

    // If D-Pad not active, use left analog stick
    if (gp->dpad == 0) {
        if (gp->axis_x < -deadzone) *axis |= 0x04;  // Left
        if (gp->axis_x > deadzone)  *axis |= 0x08;  // Right
        if (gp->axis_y < -deadzone) *axis |= 0x01;  // Up (Y is typically inverted)
        if (gp->axis_y > deadzone)  *axis |= 0x02;  // Down
    }

    // Fire button: A button (south) is primary, B button (east) is alternate
    // This matches Xbox/PS4 pattern
    if (gp->buttons & BUTTON_A) {
        *button = 1;
    } else if (gp->buttons & BUTTON_B) {
        *button = 1;
    }

    // Also check triggers as alternate fire (like Xbox)
    // Bluepad32 triggers might be 0-1023 range or normalized
    if (gp->brake > 512 || gp->throttle > 512) {
        *button = 1;
    }

    return true;
}

#ifdef __cplusplus
}
#endif

