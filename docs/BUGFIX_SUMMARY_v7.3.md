# Bug Fixes Summary - Version 7.3.0

## Branch: bugfixes-rp2350-other-controllers

## The Problem

Xbox controllers worked when first connected, but failed to work after a PS4 controller had been used:

1. Plug in Xbox → Works ✅
2. Unplug Xbox, plug in PS4 → Works ✅
3. Unplug PS4, plug in Xbox → **Doesn't work** ❌

## Root Cause Found

**Missing PS4 Unmount Callback**

When a PS4 controller was unplugged:
- TinyUSB called `tuh_hid_umount_cb()` ✅
- But `ps4_unmount_cb()` was **NEVER called** ❌
- PS4 controller stayed marked as `connected=true` forever
- `get_ps4_joystick()` kept returning success with stale data
- This blocked `get_xbox_joystick()` from ever being checked

## The Fix

**File:** `src/hid_app_host.c`

Added PS4 unmount callback wiring in `tuh_hid_umount_cb()`:

```c
// Check if this is a PS4 controller and call its unmount callback
uint16_t vid, pid;
tuh_vid_pid_get(dev_addr, &vid, &pid);
if (ps4_is_dualshock4(vid, pid)) {
    ps4_unmount_cb(dev_addr);  // ← THIS WAS MISSING!
}
```

Now when PS4 is unplugged, it properly marks as disconnected, allowing Xbox to work.

---

## Additional Fixes in 7.3.0

### 1. Xbox Controller UI Counter
- Xbox controllers now increment joystick counter on OLED
- Shows correct count when Xbox connected/disconnected

### 2. Xbox Splash Screen
- Reinstated "XBOX!" splash screen with controller type
- Shows for 3 seconds with debug info (A:X I:X C:X)

### 3. PS4 Splash Screen
- Enhanced with device address display
- Consistent timing with Xbox (2 seconds)

### 4. RP2350 (Pico 2) Support
- Full build support for Raspberry Pi Pico 2
- Use `-DPICO_BOARD=pico2` cmake option
- Output files: `atari_ikbd_pico.uf2` and `atari_ikbd_pico2.uf2`

### 5. Diagnostic Debug Page
- Detailed controller diagnostics on USB Debug page
- Can be disabled by setting `ENABLE_CONTROLLER_DEBUG 0` in config.h
- Shows HID/PS4/Xbox success counters, report reception, etc.

---

## Files Modified

**Core Fixes:**
- `src/hid_app_host.c` - Added PS4 unmount callback (THE FIX!)
- `src/main.cpp` - Xbox counter, splash screen, cleaned up debug
- `src/HidInput.cpp` - Always check Xbox/PS4 as fallback, debug counters
- `src/xinput_atari.cpp` - Simplified registration, removed verbose logging
- `src/ps4_controller.c` - Enhanced splash screen with address
- `src/UserInterface.cpp` - Conditional debug page with controller diagnostics

**Configuration:**
- `include/config.h` - Added ENABLE_CONTROLLER_DEBUG flag
- `include/version.h` - Updated to 7.3.0
- `CMakeLists.txt` - RP2350 support with board selection

**Build System:**
- `build-all.sh` - Automated build script for both boards

---

## To Disable Debug Page Later

When ready to remove the detailed debug page:

1. Edit `include/config.h`
2. Change: `#define ENABLE_CONTROLLER_DEBUG 0`
3. Rebuild

The USB Debug page will show simple device stats instead of detailed diagnostics.

---

## Testing Results

✅ Xbox controller works when first connected  
✅ Xbox controller works after being unplugged/replugged  
✅ PS4 controller works  
✅ **Xbox controller works after PS4 usage** ← THE BUG FIX!  
✅ PS4 controller works after Xbox usage  
✅ Multiple reconnection cycles work reliably  
✅ Joystick counter updates correctly  
✅ Splash screens show for both controllers  
✅ RP2350 (Pico 2) builds and runs successfully

---

## What's Next

Ready for additional controller support:
- Debug infrastructure in place
- Easy to add new controller types
- Framework proven with Xbox and PS4
- RP2350 support ready

---

**Version:** 7.3.0  
**Date:** October 22, 2025  
**Status:** ✅ **BUG FIXED AND WORKING!**  
**Branch:** bugfixes-rp2350-other-controllers

