# Release Notes

**Current Version:** 21.0.1  
**Last Updated:** January 2, 2026

---

## Version 21.0.1 (January 2, 2026)

### Improvements and Cleanup

Version 21.0.1 includes code cleanup and build script improvements following the major Bluetooth feature addition in v21.0.0.

**What's New:**
- ✅ Code cleanup and optimizations
- ✅ Build script improvements
- ✅ Production build variants (OLED=1, LOGGING=0)
- ✅ Updated splash screen design
- ✅ Debug OLED pages gated behind ENABLE_SERIAL_LOGGING=1

---

## Version 21.0.0 (December 2025)

### Major New Feature: Bluetooth Support for Pico 2 W

Version 21.0.0 introduces comprehensive **Bluetooth support** for the Raspberry Pi Pico 2 W (RP2350), enabling wireless keyboards, mice, and gamepads!

**What's New:**
- ✅ **Bluetooth Keyboard Support** - Full HID keyboard support with all keyboard shortcuts (Ctrl+F4, Ctrl+F11, etc.)
- ✅ **Bluetooth Mouse Support** - Standard HID mice with scroll wheel support (scroll wheel mapped to cursor up/down keys)
- ✅ **Bluetooth Gamepad Support** - DualSense, DualShock 4, Switch Pro, Xbox Wireless, and more via Bluepad32
- ✅ **Mix and Match** - Use any combination of USB, Bluetooth, or 9-pin Atari joystick peripherals simultaneously
- ✅ **Llamatron Dual-Stick Mode** - Works seamlessly with Bluetooth gamepads
- ✅ **Runtime Mode Selection** - Switch between USB+Bluetooth, USB only, or Bluetooth only modes via OLED interface
- ✅ **Bluetooth Pairing Management** - Easy pairing and ability to clear stored pairing keys

**Technical Details:**
- **Hardware Requirement:** Raspberry Pi Pico 2 W (RP2350) - Bluetooth requires additional RAM not available on original Pico W
- **Bluetooth Stack:** Bluepad32 library integration
- **Supported Protocols:** BR/EDR (Bluetooth Classic) for gamepads, HID for keyboards and mice
- **Pairing:** Automatic detection - just put device into Bluetooth pairing mode
- **Mode Selection:** Use left button on splash screen to cycle through USB+Bluetooth, USB only, Bluetooth only
- **Pairing Reset:** Press right button on splash screen to delete all stored Bluetooth pairing keys

**Supported Bluetooth Devices:**
- **Keyboards:** Standard HID keyboards with full shortcut support
- **Mice:** Standard HID mice with scroll wheel support
- **Gamepads:** 
  - PlayStation DualSense (PS5)
  - PlayStation DualShock 4 (PS4)
  - Nintendo Switch Pro Controller
  - Nintendo Switch JoyCons
  - Xbox Wireless Controllers
  - And more via Bluepad32

**Known Limitations:**
- Bluetooth support requires Pico 2 W (RP2350) - original Pico W (RP2040) does not have enough RAM
- Logitech MX devices require re-pairing after each emulator restart
- Bluetooth keyboards and mice work seamlessly
- All keyboard shortcuts work with Bluetooth keyboards
- Llamatron dual-stick mode works with Bluetooth gamepads

**Build Information:**
- Bluetooth builds automatically enabled for `pico2_w` board type
- Uses Bluepad32 library for gamepad support
- Standard HID protocol for keyboards and mice

---

## Version 10.0.0 (October 31, 2025)

### New Feature: PlayStation 3 DualShock 3 Support

Version 10.0.0 adds full support for the **Sony PlayStation 3 DualShock 3** controller!

**What's New:**
- ✅ PS3 DualShock 3 Controller Detection (VID:0x054C PID:0x0268)
- ✅ Proper USB initialization to activate controller
- ✅ Stops the 4 flashing lights on connection, Player 1 LED indicator activates
- ✅ Full Input Mapping:
  - D-Pad - All 8 directions with diagonal support
  - Left Analog Stick - Smooth movement control with deadzone
  - Face Buttons - Triangle, Circle, Cross (X), Square
  - Shoulder Buttons - L1, R1, L2, R2
  - Fire Button - Cross (X) button
- ✅ Clean Integration - Follows same pattern as existing controllers
- ✅ Professional splash screen on OLED display
- ✅ Seamless hot-swap support

**Technical Details:**
- PS3 Initialization Sequence requires USB Feature Report (0xF4): `{0x42, 0x0C, 0x00, 0x00}`
- Report Structure: 48 bytes with Report ID 0x01
- Stops controller searching for PlayStation console, activates LEDs, enables HID reports

**Files Added:**
- `include/ps3_controller.h` - PS3 API definitions
- `src/ps3_controller.c` - Complete implementation

**Supported Controllers (v10.0.0):**
- ✅ USB HID Joysticks
- ✅ **PlayStation 3 DualShock 3** ⭐ NEW!
- ✅ PlayStation 4 DualShock 4
- ✅ Xbox One / Xbox 360 (XInput)
- ✅ Nintendo Switch Pro Controller
- ✅ PowerA Fusion Wireless Arcade Stick
- ✅ Google Stadia Controller

**Build Information:**
- Firmware Sizes: RP2040 (Pico): 335KB | RP2350 (Pico 2): 318KB
- Debug Mode: Disabled by default (use `DEBUG=1 ./build-all.sh` to enable)

---

## Version 9.0.0 (October 29, 2025)

### Major New Features

#### Google Stadia Controller - FULLY SUPPORTED! ⭐

Version 9.0.0 adds **complete support** for the Google Stadia Controller (VID 0x18D1, PID 0x9400).

**What Works:**
- ✅ D-Pad 8-direction control
- ✅ Left analog stick (smooth movement)
- ✅ Right analog stick (available for future features)
- ✅ All face buttons (A, B, X, Y) as fire
- ✅ Shoulder buttons (L1, R1) as fire
- ✅ Analog triggers (L2, R2) as fire
- ✅ Splash screen on connection
- ✅ Hot-swap support

**Implementation:**
- Based on stadia-vigem project format
- Custom byte-level parsing (HID descriptor incomplete)
- D-Pad priority over analog stick for precise control
- First input report triggers immediate registration

#### Externalized Debug System

All debug displays now controlled by single environment variable:

```bash
# Production build (default) - Debug OFF
./build-all.sh

# Debug build - Shows all detection screens
DEBUG=1 ./build-all.sh
```

**Benefits:**
- 3KB smaller production builds (323K vs 326K)
- Better performance (no debug overhead)
- Easy troubleshooting when needed
- Single flag controls all debug features

**Debug Features Controlled:**
- Controller detection OLED screens
- USB diagnostic counters
- Switch/Stadia verbose logging
- All troubleshooting displays

**Controller Support Matrix:**
- Microsoft Xbox 360, Xbox One, Xbox Series X|S
- Sony PS4 DualShock 4 (v1, v2)
- Nintendo Switch Pro (v1)
- PowerA Fusion Arcade (v1, v2), Wired Plus
- **Google Stadia** ⭐ NEW
- Generic USB HID Joysticks

**Total Supported:** 12+ controller types across 6 manufacturers!

**Firmware Size:**
- Production (DEBUG=0): 323K (RP2040), 306K (RP2350)
- Debug (DEBUG=1): 326K (RP2040), 309K (RP2350)

---

## Version 8.0.0 (October 28, 2025)

### Major New Feature: PowerA Arcade Stick Support

Version 8.0.0 adds full support for **PowerA Fusion Wireless Arcade Stick** controllers designed for Nintendo Switch.

**What Was Fixed:**
The PowerA Fusion Wireless Arcade Stick (newer hardware revision with PID 0xA715) was being incorrectly detected as a mouse because:
1. The device wasn't in the Switch controller detection list
2. It has X/Y axes which caused the HID parser to classify it as a mouse
3. This prevented it from working as a joystick

**Solution:**
- Added `POWERA_FUSION_ARCADE_V2` definition for PID 0xA715
- Updated `switch_is_controller()` to recognize this hardware revision
- Controller now properly detected and works as **Joystick 0 or 1**

**Supported PowerA Controllers:**
- Fusion Wireless Arcade Stick (v1) - VID:0x20D6 PID:0xA711 ✅
- Fusion Wireless Arcade Stick (v2) - VID:0x20D6 PID:0xA715 ✅ **NEW in v8.0.0**
- Wired Controller Plus - VID:0x20D6 PID:0xA712 ✅
- Wireless Controller - VID:0x20D6 PID:0xA713 ✅

**Updated Controller Support:**
- Xbox Controllers: Xbox 360, Xbox One, Xbox Series X|S, Original Xbox
- PlayStation Controllers: PS4 DualShock 4 (wired USB)
- Nintendo Switch Controllers: Switch Pro Controller, **PowerA Fusion Wireless Arcade Stick** ⭐ NEW
- Generic Controllers: Standard USB HID joysticks and gamepads

**Firmware Size:** RP2040: 321K | RP2350: 304K

---

## Version 7.3.0 (October 22, 2025)

### Critical Bug Fix

**Fixed Xbox Controller Reconnection Issue**

**Problem:**
Xbox controllers stopped working after PS4 controller usage:
- Xbox first → Works ✅
- PS4 used → Works ✅
- Xbox after PS4 → Broken ❌

**Root Cause:**
PS4 unmount callback was never being called when controller was unplugged. The PS4 controller stayed marked as "connected" with stale data, blocking Xbox from being checked as a fallback input source.

**The Fix:**
Added proper PS4 unmount callback wiring in `hid_app_host.c`. Now when PS4 is disconnected, it properly cleans up and allows Xbox to work.

**Result:**
- ✅ Xbox and PS4 controllers can now be swapped freely
- ✅ Both controllers work reliably in any order
- ✅ Multiple reconnection cycles work correctly

### New Features

1. **RP2350 (Raspberry Pi Pico 2) Support**
   - Full support for building firmware for Pico 2
   - Use cmake option: `-DPICO_BOARD=pico2`
   - Automatic output naming: `atari_ikbd_pico2.uf2`
   - Fully tested and working on RP2350 hardware

2. **Enhanced Controller Detection**
   - Xbox controllers now increment OLED joystick counter
   - "XBOX!" splash screen shows controller type and debug info
   - PS4 splash screen enhanced with device address
   - Both splash screens display for 2-3 seconds

3. **Build Automation**
   - New `build-all.sh` script builds both Pico and Pico 2 firmware
   - Outputs to `dist/` directory for easy access
   - Single command builds both variants

4. **Debug Diagnostics Page (Optional)**
   - Detailed controller debug info on USB Debug page
   - Shows HID/PS4/Xbox success counters
   - Tracks report reception and data flow
   - Can be disabled: Set `ENABLE_CONTROLLER_DEBUG 0` in config.h

**Tested Controllers:**
- ✅ Xbox One wired, Xbox Series X|S, Xbox 360 wired, Xbox 360 wireless (with receiver)
- ✅ PS4 DualShock 4 (wired)
- ✅ Generic USB joysticks, Logitech controllers

**Firmware Size:** 
- RP2040: 311K (`atari_ikbd_pico.uf2`)
- RP2350: 295K (`atari_ikbd_pico2.uf2`)

---

## Earlier Versions

Versions prior to 7.3.0 were documented in earlier formats. Key milestones include:

- **Version 7.0.0:** Full Xbox Controller Support integration using official TinyUSB XInput driver
- **Version 3.3.0-3.4.0:** Serial Communication Optimizations (RX polling improved 500x, UART FIFO enabled)
- **Version 3.0+:** Quality of Life Features (9 keyboard shortcuts, multilingual UI, Logitech Unifying Receiver Support)

---

## Upgrade Instructions

To upgrade to the latest version:

1. Download the firmware from the `dist/` directory:
   - `atari_ikbd_pico.uf2` for Raspberry Pi Pico (RP2040)
   - `atari_ikbd_pico2.uf2` for Raspberry Pi Pico 2 (RP2350)
2. Hold BOOTSEL button on your Pico
3. Connect USB cable
4. Release BOOTSEL button
5. Copy the .uf2 file to the RPI-RP2 drive
6. The Pico will automatically reboot with the new firmware

**Note:** All upgrades are backward compatible - no configuration changes required. Simply flash the new firmware and all existing functionality continues to work.

---

## Build Instructions

### Standard Build:
```bash
./build-all.sh
```

### With Options:
```bash
# Enable debug displays
DEBUG=1 ./build-all.sh

# French interface
LANGUAGE=FR ./build-all.sh

# German interface with debug
LANGUAGE=DE DEBUG=1 ./build-all.sh
```

**Outputs:**
- `dist/atari_ikbd_pico.uf2` - Raspberry Pi Pico
- `dist/atari_ikbd_pico2.uf2` - Raspberry Pi Pico 2

---

## Repository

**GitHub:** https://github.com/trickydee/ultramegausb-atari-st-rpikbd

**Full Changelog:** See git history for detailed commit information

---

*For detailed technical documentation, see the `docs/` directory.*

