# Atari ST USB Adapter - Keyboard Shortcuts Reference

## v4.0.0 Keyboard Shortcuts

### System Control Shortcuts

| Shortcut | Function | Description |
|----------|----------|-------------|
| **Ctrl+F12** | Toggle Mouse Mode | Switches between USB mouse and D-SUB joystick 0 |
| **Ctrl+F11** | XRESET | Triggers HD6301 hardware reset (shows "RESET" on OLED) |

### Clock Speed Control

| Shortcut | Function | Description |
|----------|----------|-------------|
| **Alt+Plus** | Set 270MHz | Overclocks RP2040 to 270MHz for maximum performance |
| **Alt+Minus** | Set 150MHz | Sets RP2040 to 150MHz for stability |

### Atari ST Key Mapping

| Shortcut | Function | Description |
|----------|----------|-------------|
| **Alt+/** | INSERT Key | Sends Atari ST INSERT key (useful for modern keyboards) |

---

## Detailed Descriptions

### Ctrl+F12 - Mouse Mode Toggle

**Purpose:** Switch between USB mouse and D-SUB joystick 0

**How it works:**
- When USB mouse is enabled: Mouse movements sent to Atari ST
- When disabled: D-SUB joystick 0 is active instead
- OLED display shows current mode
- Includes debouncing to prevent accidental toggles

**Use case:** Games that need joystick 0 instead of mouse

---

### Ctrl+F11 - XRESET (HD6301 Reset)

**Purpose:** Reset the HD6301 emulator (equivalent to power cycling the IKBD)

**What happens:**
1. OLED shows "RESET" and "Ctrl+F11" message
2. Waits 500ms for visual confirmation
3. Triggers cold reset of HD6301 emulator
4. ROM firmware restarts and sends 0xF1 packet
5. Splash screen appears

**Use case:** 
- Fix stuck IKBD states
- Reset after mode switching issues
- Debug IKBD communication problems
- Equivalent to unplugging/replugging the original IKBD

---

### Alt+Plus - 270MHz Overclock

**Purpose:** Set RP2040 CPU to maximum speed for best performance

**What happens:**
- CPU clock changes from current speed to 270MHz
- All processing becomes 80% faster
- Better serial communication performance
- More responsive IKBD command processing

**Use case:** 
- Games with high input requirements
- Complex IKBD mode switching
- Maximum performance needed

---

### Alt+Minus - 150MHz Stability

**Purpose:** Set RP2040 CPU to stable speed

**What happens:**
- CPU clock changes from current speed to 150MHz
- More conservative power usage
- Stable timing for sensitive applications
- Default speed on boot

**Use case:**
- Games that are sensitive to timing
- Power-constrained environments
- Stable baseline performance

---

### Alt+/ - INSERT Key

**Purpose:** Send Atari ST INSERT key from modern keyboard

**What happens:**
- When Alt+/ is pressed, sends Atari INSERT key (scancode 82)
- The / key itself is not sent to Atari ST
- Works with both Left Alt and Right Alt

**Use case:**
- Modern keyboards don't have dedicated INSERT key
- Atari ST applications that need INSERT key
- Text editors and word processors

---

## Technical Details

### Key Detection

All shortcuts are detected in `src/HidInput.cpp` in the `handle_keyboard()` function:

```cpp
// Modifier detection
bool ctrl_pressed = (kb->modifier & KEYBOARD_MODIFIER_LEFTCTRL) || 
                   (kb->modifier & KEYBOARD_MODIFIER_RIGHTCTRL);
bool alt_pressed = (kb->modifier & KEYBOARD_MODIFIER_LEFTALT) || 
                  (kb->modifier & KEYBOARD_MODIFIER_RIGHTALT);

// Key detection
for (int i = 0; i < 6; ++i) {
    if (kb->keycode[i] == TARGET_KEY) {
        // Handle shortcut
    }
}
```

### Key Codes Used

| Key | HID Keycode | Decimal | Notes |
|-----|-------------|---------|-------|
| F11 | 0x44 | 68 | XRESET trigger |
| F12 | 0x45 | 69 | Mouse toggle |
| / | 0x38 | 56 | INSERT key mapping |
| +/= | 0x2E | 46 | Clock speed up |
| - | 0x2D | 45 | Clock speed down |

### Blocking from Atari ST

All shortcut keys are blocked from being sent to the Atari ST:

```cpp
// If Ctrl+F11, don't send to Atari (used for XRESET)
else if (ctrl_pressed && kb->keycode[i] == XRESET_KEY) {
    st_keys[i] = 0;
}
```

This prevents the Atari ST from seeing the shortcut keys.

---

## Visual Feedback

### OLED Display Messages

| Shortcut | OLED Display |
|----------|--------------|
| **Ctrl+F11** | "RESET" (large) + "Ctrl+F11" (small) |
| **Ctrl+F12** | Mode change shown in status display |
| **Alt+Plus** | Clock speed change (if debug enabled) |
| **Alt+Minus** | Clock speed change (if debug enabled) |
| **Alt+/** | No display (key sent to Atari) |

### Status Page

The OLED status page shows:
- Current mouse/joystick mode
- USB device connections
- Version number (v4.0.0)

---

## Compatibility

### Keyboard Support

- **USB keyboards:** Full support for all shortcuts
- **Mac keyboards:** All shortcuts work (tested with Alt+0 issue resolved)
- **PC keyboards:** Full support
- **Wireless keyboards:** Should work if USB HID compliant

### Atari ST Compatibility

- All shortcuts are transparent to the Atari ST
- No interference with Atari ST software
- Original IKBD behavior preserved
- Enhanced with modern keyboard features

---

## Troubleshooting

### Shortcuts Not Working

1. **Check keyboard:** Ensure USB keyboard is connected
2. **Check modifiers:** Press Ctrl/Alt keys fully
3. **Check timing:** Hold modifier, then press function key
4. **Check OLED:** Look for visual feedback messages

### Common Issues

**Ctrl+F11 not working:**
- Make sure you're pressing Ctrl+F11 (not Alt+F11)
- Check OLED for "RESET" message
- Try holding Ctrl first, then F11

**Alt+/ not working:**
- Make sure you're pressing Alt+/ (not Ctrl+/)
- The / key should be sent to Atari as INSERT
- Check if Atari ST receives the key

**Clock speed not changing:**
- Alt+Plus and Alt+Minus should work
- Check console output for clock change messages
- Default is 270MHz on boot

---

## Version History

### v4.0.0 (Current)
- All shortcuts implemented and working
- XRESET via Ctrl+F11
- Clock control via Alt+/Alt-
- INSERT key via Alt+/

### v3.4.0
- XRESET implementation
- Serial communication optimizations

### v3.3.0
- Mouse toggle via Ctrl+F12
- INSERT key via Alt+/

### v3.1.0
- On-demand joystick polling
- Basic keyboard shortcuts

---

## Summary

The Atari ST USB adapter now supports **5 keyboard shortcuts**:

1. **Ctrl+F12** - Toggle mouse/joystick mode
2. **Ctrl+F11** - Reset IKBD (XRESET)
3. **Alt+Plus** - Set 270MHz (performance)
4. **Alt+Minus** - Set 150MHz (stability)
5. **Alt+/** - Send INSERT key

All shortcuts are designed to be:
- **Non-interfering** with Atari ST software
- **Visually confirmed** via OLED display
- **Easy to remember** with logical key combinations
- **Compatible** with modern USB keyboards

These shortcuts provide full control over the adapter's behavior while maintaining complete compatibility with the Atari ST's original IKBD interface.

---

**Version:** 4.0.0  
**Date:** October 15, 2025  
**Status:** ✅ All shortcuts implemented and tested
