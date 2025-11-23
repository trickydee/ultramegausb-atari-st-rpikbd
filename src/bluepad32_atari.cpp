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
    const int32_t DEAD_ZONE = 8000;  // Bluepad32 uses -512 to 511 range, so ~15% deadzone

    // D-Pad has priority (like other controllers)
    if (gp->dpad & DPAD_UP)    *axis |= 0x01;
    if (gp->dpad & DPAD_DOWN)  *axis |= 0x02;
    if (gp->dpad & DPAD_LEFT)  *axis |= 0x04;
    if (gp->dpad & DPAD_RIGHT) *axis |= 0x08;

    // If D-Pad not active, use left analog stick
    if (gp->dpad == 0) {
        if (gp->axis_x < -DEAD_ZONE) *axis |= 0x04;  // Left
        if (gp->axis_x > DEAD_ZONE)  *axis |= 0x08;  // Right
        if (gp->axis_y < -DEAD_ZONE) *axis |= 0x01;  // Up (Y is typically inverted)
        if (gp->axis_y > DEAD_ZONE)  *axis |= 0x02;  // Down
    }

    // Fire button: A button (south) is primary, B button (east) is alternate
    // This matches Xbox/PS4 pattern
    if (gp->buttons & BUTTON_A) {
        *button = 1;
    } else if (gp->buttons & BUTTON_B) {
        *button = 1;
    }

    // Also check triggers as alternate fire (like Xbox)
    if (gp->brake > 512 || gp->throttle > 512) {
        *button = 1;
    }

    return true;
}

#ifdef __cplusplus
}
#endif

