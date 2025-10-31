# Release Notes - Version 10.0.0

**Release Date:** October 31, 2025  
**Branch:** ps3-ds3-controller → main

---

## 🎮 New Feature: PlayStation 3 DualShock 3 Support

Version 10.0.0 adds full support for the **Sony PlayStation 3 DualShock 3** controller!

### What's New

✅ **PS3 DualShock 3 Controller Detection**
- Automatic VID/PID detection (0x054C:0x0268)
- Proper USB initialization to activate controller
- Stops the 4 flashing lights on connection
- Player 1 LED indicator activates

✅ **Full Input Mapping**
- **D-Pad** - All 8 directions with diagonal support
- **Left Analog Stick** - Smooth movement control with deadzone
- **Face Buttons** - Triangle, Circle, Cross (X), Square
- **Shoulder Buttons** - L1, R1, L2, R2
- **Fire Button** - Cross (X) button

✅ **Clean Integration**
- Follows same pattern as existing PS4, Xbox, Switch, and Stadia controllers
- Professional splash screen on OLED display
- Console debug output for troubleshooting
- Seamless hot-swap support

---

## Technical Details

### PS3 Initialization Sequence
The PS3 DualShock 3 requires a special USB Feature Report (0xF4) to activate:
```c
{0x42, 0x0C, 0x00, 0x00}  // PS3 enable command
```

This initialization:
- Stops the controller searching for a PlayStation console
- Activates the controller LEDs (Player 1 indicator)
- Enables HID report transmission

### Report Mapping
**Report Structure (48 bytes):**
- Byte 0: Report ID (0x01)
- Byte 1: Buttons (SELECT, L3, R3, START)
- Byte 2: D-Pad bitmask (UP=0x10, RIGHT=0x20, DOWN=0x40, LEFT=0x80)
- Byte 3: Face/Shoulder buttons (L2, R2, L1, R1, Triangle, Circle, X, Square)
- Bytes 6-9: Analog sticks (LX, LY, RX, RY) - 0x80 = center
- Bytes 18-19: Analog triggers (L2, R2) - 0x00-0xFF

### Files Modified
**New Files:**
- `include/ps3_controller.h` - PS3 API definitions
- `src/ps3_controller.c` - Complete implementation

**Modified Files:**
- `src/hid_app_host.c` - Added PS3 detection, mount, report processing, unmount
- `src/HidInput.cpp` - Added `get_ps3_joystick()` function
- `include/HidInput.h` - Added function declaration
- `CMakeLists.txt` - Added ps3_controller.c to build
- `build-all.sh` - Updated version string
- `include/version.h` - Bumped to 9.1.0

---

## Credits

PS3 report format based on:
- Linux kernel PS3 driver
- [PlayStation-3-Controller-for-Raspberry](https://github.com/trickydee/PlayStation-3-Controller-for-Raspberry) reference implementation

---

## Supported Controllers (v9.1.0)

✅ USB HID Joysticks  
✅ **PlayStation 3 DualShock 3** ⭐ NEW!  
✅ PlayStation 4 DualShock 4  
✅ Xbox One / Xbox 360 (XInput)  
✅ Nintendo Switch Pro Controller  
✅ PowerA Fusion Wireless Arcade Stick  
✅ Google Stadia Controller  

---

## Build Information

**Firmware Sizes:**
- RP2040 (Pico): 335KB
- RP2350 (Pico 2): 318KB

**Debug Mode:** Disabled by default (use `DEBUG=1 ./build-all.sh` to enable)

---

## Testing

All features tested and verified:
- ✅ Controller detection and initialization
- ✅ D-Pad movement (8 directions)
- ✅ Left analog stick control
- ✅ Fire button (Cross/X)
- ✅ Hot-swap functionality
- ✅ OLED splash screen
- ✅ Compatibility with other controllers

---

## Upgrade Instructions

1. Download `atari_ikbd_pico.uf2` (RP2040) or `atari_ikbd_pico2.uf2` (RP2350)
2. Hold BOOTSEL on your Pico and connect USB
3. Copy the .uf2 file to the RPI-RP2 drive
4. Connect your PS3 DualShock 3 controller
5. Enjoy! 🎮

---

**Full Changelog:** https://github.com/trickydee/ultramegausb-atari-st-rpikb

