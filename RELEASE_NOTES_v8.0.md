# Release Notes - Version 8.0.0

**Date:** October 28, 2025  
**Branch:** switch-controller-compatability

## 🎮 Major New Feature: PowerA Arcade Stick Support

Version 8.0.0 adds full support for **PowerA Fusion Wireless Arcade Stick** controllers designed for Nintendo Switch.

### What Was Fixed

The PowerA Fusion Wireless Arcade Stick (newer hardware revision with PID 0xA715) was being incorrectly detected as a mouse because:

1. The device wasn't in the Switch controller detection list
2. It has X/Y axes which caused the HID parser to classify it as a mouse
3. This prevented it from working as a joystick

### Solution

- Added `POWERA_FUSION_ARCADE_V2` definition for PID 0xA715
- Updated `switch_is_controller()` to recognize this hardware revision
- Controller now properly detected and works as **Joystick 0 or 1**

### Supported PowerA Controllers

| Model | VID | PID | Status |
|-------|-----|-----|--------|
| Fusion Wireless Arcade Stick (v1) | 0x20D6 | 0xA711 | ✅ Supported |
| Fusion Wireless Arcade Stick (v2) | 0x20D6 | 0xA715 | ✅ **NEW in v8.0.0** |
| Wired Controller Plus | 0x20D6 | 0xA712 | ✅ Supported |
| Wireless Controller | 0x20D6 | 0xA713 | ✅ Supported |

## Updated Controller Support List

### Xbox Controllers
- Xbox 360 (wired/wireless)
- Xbox One
- Xbox Series X|S
- Original Xbox

### PlayStation Controllers
- PS4 DualShock 4 (wired USB)

### Nintendo Switch Controllers
- Nintendo Switch Pro Controller
- PowerA Fusion Wireless Arcade Stick ⭐ **NEW**
- Other PowerA third-party controllers

### Generic Controllers
- Standard USB HID joysticks and gamepads

## Keyboard Shortcuts

All existing keyboard shortcuts remain functional:

| Shortcut | Function |
|----------|----------|
| **Ctrl+F12** | Toggle Mouse Mode (Mouse ↔ Joystick 0) |
| **Ctrl+F11** | XRESET (HD6301 hardware reset) |
| **Ctrl+F10** | Toggle Joystick 1 (D-SUB ↔ USB) |
| **Ctrl+F9** | Toggle Joystick 0 (D-SUB ↔ USB) |
| **Ctrl+F8** | Restore Joystick Mode (send 0x14 command) |
| **Alt+/** | INSERT key |
| **Alt+[** | Keypad / |
| **Alt+]** | Keypad * |
| **Alt++** | Set 270MHz overclock |
| **Alt+-** | Set 150MHz stable mode |

## Version Changes

- **Major version bump:** 7.5.0 → **8.0.0**
  - Justification: New controller hardware support is a significant feature addition

## Files Modified

### Controller Support
- `include/switch_controller.h` - Added POWERA_FUSION_ARCADE_V2 (0xA715)
- `src/switch_controller.c` - Updated detection and mount callbacks
- `src/hid_app_host.c` - Enhanced VID/PID debugging for OLED display

### Documentation
- `README.md` - Added PowerA Fusion Wireless Arcade Stick to supported controllers
- `include/version.h` - Bumped to 8.0.0
- `build-all.sh` - Updated version display to 8.0.0

## OLED Display

When plugging in a PowerA arcade stick, you'll now see:

**Screen 1** (3 seconds):
```
HID!!
Addr:1 Inst:0
VID:20D6 PID:A715
P:0 L:80
```

**Screen 2** (3 seconds):
```
SWITCH!
is_switch=1
PID:A715
```

**Screen 3** (2 seconds):
```
SWITCH!
PowerA Arcade
Addr:1
```

## Testing Results

✅ **PowerA Fusion Wireless Arcade Stick**
- Correctly detected as Switch controller
- Joystick direction (D-Pad) working
- Fire buttons working
- No longer misdetected as mouse

## Known Issues

None related to this release.

## Upgrade Notes

Flash the new firmware from `dist/`:
- `atari_ikbd_pico.uf2` for Raspberry Pi Pico (RP2040)
- `atari_ikbd_pico2.uf2` for Raspberry Pi Pico 2 (RP2350)

No configuration changes required - PowerA arcade sticks will work immediately upon connection.

## Credits

- Controller detection debugging using OLED-only methodology
- Mac USB device info used to identify correct PID (42773 decimal = 0xA715 hex)

---

**Build Status:** ✅ Complete  
**Firmware Size:** RP2040: 321K | RP2350: 304K  
**Branch:** switch-controller-compatability


