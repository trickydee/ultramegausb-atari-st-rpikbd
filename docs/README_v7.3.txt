================================================================================
ATARI ST USB ADAPTER - Version 7.3.0
Branch: bugfixes-rp2350-other-controllers
================================================================================

✅ BUG FIXED: Xbox and PS4 controllers now work reliably when swapped!

================================================================================
QUICK START
================================================================================

FIRMWARE FILES:
  dist/atari_ikbd_pico.uf2   (311K) - For Raspberry Pi Pico (RP2040)
  dist/atari_ikbd_pico2.uf2  (295K) - For Raspberry Pi Pico 2 (RP2350)

FLASH INSTRUCTIONS:
  1. Hold BOOTSEL button on Pico/Pico 2
  2. Connect USB cable
  3. Release BOOTSEL button
  4. Copy .uf2 file to RPI-RP2 drive

REBUILD:
  ./build-all.sh  ← Builds both versions automatically

================================================================================
WHAT WAS FIXED
================================================================================

THE BUG:
  Xbox controller stopped working after PS4 controller was used and removed.

THE FIX:
  PS4 unmount callback is now properly called when PS4 is unplugged,
  preventing stale PS4 state from blocking Xbox detection.

ALSO FIXED:
  ✅ Xbox controllers now show in joystick counter on OLED
  ✅ Xbox splash screen reinstated ("XBOX!" with controller type)
  ✅ RP2350 (Pico 2) support added
  ✅ Automated build script for both boards

================================================================================
DEBUG PAGE (Currently Enabled)
================================================================================

Press middle button to navigate to "Debug v7.3" page.

Shows:
  - HID/PS4/Xbox success counters (which controller is active)
  - Xbox report reception counter
  - Controller state and address info
  - Device counts

TO DISABLE LATER:
  Edit include/config.h
  Change: #define ENABLE_CONTROLLER_DEBUG 0
  Rebuild

================================================================================
SUPPORTED CONTROLLERS (v7.3.0)
================================================================================

Xbox Controllers:
  ✅ Xbox One (wired)
  ✅ Xbox Series X|S
  ✅ Xbox 360 wired
  ✅ Xbox 360 wireless (with receiver)
  ✅ Xbox OG

PlayStation Controllers:
  ✅ PS4 DualShock 4 (wired)

HID Controllers:
  ✅ Generic USB joysticks
  ✅ Various HID-compliant gamepads

All controllers can be swapped freely with no issues!

================================================================================
SPLASH SCREENS (Kept as Requested)
================================================================================

Xbox Controller:
  Shows "XBOX!" with controller type (Xbox One, 360 Wired, etc.)
  Displays for 3 seconds
  Shows: A:X I:X C:X debug info

PS4 Controller:
  Shows "PS4" with "DualShock 4"
  Displays for 2 seconds
  Shows: Addr:X

================================================================================
FOR NEXT SESSION
================================================================================

Ready to add more controllers!
  - Debug infrastructure in place
  - Framework tested and working
  - Easy to add new controller types

To disable debug page when done:
  Set ENABLE_CONTROLLER_DEBUG 0 in include/config.h

================================================================================

Version: 7.3.0
Status: ✅ WORKING - Bug Fixed!
Date: October 22, 2025

================================================================================



