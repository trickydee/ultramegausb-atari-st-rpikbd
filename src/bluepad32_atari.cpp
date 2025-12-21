/*
 * Bluepad32 to Atari ST Joystick Converter
 * Converts Bluepad32 gamepad format to Atari ST joystick format
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>  // for abs()
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

    // Dead zone for analog sticks.
    //
    // NOTE: In practice, some Bluetooth gamepads (notably Xbox over Bluetooth)
    // report a much smaller axis range (roughly -512..+511) instead of the
    // documented -32768..+32767 range. A large deadzone (e.g. 8000) would
    // therefore swallow all movement.
    //
    // Heuristic:
    // - If the maximum absolute axis value is small (<= 1000), assume a
    //   "small-range" device and use a tiny deadzone (~80 units).
    // - Otherwise fall back to a larger deadzone (~8000) for full-range axes.
    int32_t ax = gp->axis_x;
    int32_t ay = gp->axis_y;
    int32_t max_abs = ax;
    if (abs(ay) > abs(max_abs)) {
        max_abs = ay;
    }
    max_abs = abs(max_abs);

    int32_t deadzone;
    if (max_abs <= 1000) {
        // Small-range stick (e.g. -512..+511): use small deadzone
        deadzone = 80;
    } else {
        // Full-range stick: use ~25% deadzone (similar to our USB/XInput mapping)
        deadzone = 8000;
    }

    // D-Pad has priority (like other controllers)
    if (gp->dpad & DPAD_UP)    *axis |= 0x01;
    if (gp->dpad & DPAD_DOWN)  *axis |= 0x02;
    if (gp->dpad & DPAD_LEFT)  *axis |= 0x04;
    if (gp->dpad & DPAD_RIGHT) *axis |= 0x08;

    // If D-Pad not active, use left analog stick
    if (gp->dpad == 0) {
        if (ax < -deadzone) *axis |= 0x04;  // Left
        if (ax > deadzone)  *axis |= 0x08;  // Right
        if (ay < -deadzone) *axis |= 0x01;  // Up (Y is typically inverted)
        if (ay > deadzone)  *axis |= 0x02;  // Down
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

// Extract Llamatron dual-stick axes from Bluepad32 gamepad
// Similar to ps4_llamatron_axes, xinput_llamatron_axes, etc.
// Returns true if gamepad data was successfully extracted
bool bluepad32_llamatron_axes(uint8_t* joy1_axis, uint8_t* joy1_fire,
                               uint8_t* joy0_axis, uint8_t* joy0_fire) {
    if (!joy1_axis || !joy1_fire || !joy0_axis || !joy0_fire) {
        return false;
    }
    
    // Get first connected Bluetooth gamepad
    uni_gamepad_t gp;
    if (!bluepad32_get_gamepad(0, &gp)) {
        return false;
    }
    
    const uni_gamepad_t* gamepad = &gp;
    
    // Deadzone calculation (same as bluepad32_to_atari_joystick)
    int32_t ax = gamepad->axis_x;
    int32_t ay = gamepad->axis_y;
    int32_t max_abs = abs(ax);
    if (abs(ay) > max_abs) {
        max_abs = abs(ay);
    }
    
    int32_t deadzone;
    if (max_abs <= 1000) {
        deadzone = 80;  // Small-range stick
    } else {
        deadzone = 8000;  // Full-range stick
    }
    
    // Joy1: Left stick (axis_x, axis_y)
    *joy1_axis = 0;
    
    // D-Pad has priority for left stick
    if (gamepad->dpad & DPAD_UP)    *joy1_axis |= 0x01;
    if (gamepad->dpad & DPAD_DOWN)  *joy1_axis |= 0x02;
    if (gamepad->dpad & DPAD_LEFT)  *joy1_axis |= 0x04;
    if (gamepad->dpad & DPAD_RIGHT) *joy1_axis |= 0x08;
    
    // If D-Pad not active, use left analog stick
    if (gamepad->dpad == 0) {
        if (ax < -deadzone) *joy1_axis |= 0x04;  // Left
        if (ax > deadzone)  *joy1_axis |= 0x08;  // Right
        if (ay < -deadzone) *joy1_axis |= 0x01;  // Up
        if (ay > deadzone)  *joy1_axis |= 0x02;  // Down
    }
    
    // Joy1 fire: B button (matches other controllers)
    *joy1_fire = (gamepad->buttons & BUTTON_B) ? 1 : 0;
    
    // Joy0: Right stick (axis_rx, axis_ry)
    *joy0_axis = 0;
    
    // Use right analog stick for Joy0
    int32_t rx = gamepad->axis_rx;
    int32_t ry = gamepad->axis_ry;
    
    if (rx < -deadzone) *joy0_axis |= 0x04;  // Left
    if (rx > deadzone)  *joy0_axis |= 0x08;  // Right
    if (ry < -deadzone) *joy0_axis |= 0x01;  // Up
    if (ry > deadzone)  *joy0_axis |= 0x02;  // Down
    
    // Joy0 fire: A button (matches other controllers)
    *joy0_fire = (gamepad->buttons & BUTTON_A) ? 1 : 0;
    
    return true;
}

#ifdef __cplusplus
}
#endif

