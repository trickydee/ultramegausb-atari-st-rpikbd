# PowerA Fusion Wireless Arcade Stick Implementation Summary

**Date:** October 28, 2025  
**Version:** 8.0.0  
**Branch:** switch-controller-compatability  
**Status:** ✅ **COMPLETE AND WORKING**

---

## 🎯 Problem

The PowerA Fusion Wireless Arcade Stick for Nintendo Switch was being detected as a **mouse** instead of a **joystick**, making it unusable for gaming on the Atari ST.

## 🔍 Root Cause Analysis

### Initial Detection

Using OLED-only debugging (no serial console available), we discovered:

**Device Information:**
- **VID:** 0x20D6 (PowerA/BDA vendor)
- **PID:** 0xA715 (newer hardware revision)
- **Protocol:** 0 (Generic HID, not boot mouse)
- **Descriptor Length:** 80 bytes
- **USB Product Name:** "Switch Controller"
- **Firmware Version:** V1.2.7_200324_0

### The Bug

The code had `POWERA_FUSION_ARCADE` defined as **PID 0xA711**, but this specific unit has **PID 0xA715** (a newer hardware revision).

Because the PID didn't match:
1. `switch_is_controller(0x20D6, 0xA715)` returned `false`
2. Device went through generic HID parser
3. Parser detected X/Y axes → classified as mouse
4. Joystick functionality completely broken

## ✅ Solution Implemented

### Code Changes

**1. Added New Product ID** (`include/switch_controller.h`)
```c
#define POWERA_FUSION_ARCADE_V2 0xA715  // Fusion Wireless Arcade Stick (newer version)
```

**2. Updated Detection Logic** (`src/switch_controller.c`)
```c
// PowerA third-party controllers
if (vid == POWERA_VENDOR_ID) {
    switch (pid) {
        case POWERA_FUSION_ARCADE:
        case POWERA_FUSION_ARCADE_V2:  // Added
        case POWERA_WIRED_PLUS:
        case POWERA_WIRELESS:
            return true;
    }
}
```

**3. Updated Mount Callbacks**
- Added PID check to display "PowerA Arcade" on OLED
- Added name to console logging for easier debugging

**4. Enhanced OLED Debugging** (`src/hid_app_host.c`)
- Added VID/PID display on device connection
- Added Switch detection verification display
- All debug code wrapped in `#if 0` blocks for production

### Documentation Updates

**1. README.md**
- Added "PowerA Fusion Wireless Arcade Stick for Nintendo Switch" to controller list
- Updated USB Controller Support section with v8.0.0 note
- Ctrl+F8 shortcut already documented (no changes needed)

**2. Version Bump**
- `include/version.h`: 7.5.0 → **8.0.0**
- `build-all.sh`: Updated version display
- Major version bump justified by new hardware support

**3. Release Notes**
- Created `RELEASE_NOTES_v8.0.md` with full documentation
- Documented all changes, testing, and known controller PIDs

## 🧪 Testing Results

### Before Fix
```
HID!!
VID:20D6 PID:A715
is_switch=0  ❌
→ Detected as MOUSE
→ Not functional as joystick
```

### After Fix
```
HID!!
VID:20D6 PID:A715
is_switch=1  ✅

SWITCH!
PowerA Arcade
Addr:1

→ Detected as JOYSTICK
→ Fully functional ✅
```

### Functionality Verified
- ✅ D-Pad directions working
- ✅ Fire buttons working
- ✅ Assigned to Joystick 0/1
- ✅ No mouse interference
- ✅ Hot-swap working

## 📊 PowerA Controller Database

| Model | VID | PID | Firmware | Status |
|-------|-----|-----|----------|--------|
| Fusion Arcade v1 | 0x20D6 | 0xA711 | Unknown | ✅ Supported |
| Fusion Arcade v2 | 0x20D6 | 0xA715 | V1.2.7_200324 | ✅ **Added v8.0.0** |
| Wired Plus | 0x20D6 | 0xA712 | Unknown | ✅ Supported |
| Wireless | 0x20D6 | 0xA713 | Unknown | ✅ Supported |

## 🔧 Debug Methodology

Since only OLED display was available (no serial console), we used:

1. **VID/PID Display:** Show device identification on mount
2. **Detection Verification:** Show `is_switch` boolean result
3. **Progressive Testing:** Enable debug, flash, test, document
4. **Mac USB Info:** Used `system_profiler SPUSBDataType` to get decimal PID

**Key Learning:** Decimal 42773 = Hex 0xA715 (conversion critical!)

## 📦 Build Information

**Version:** 8.0.0  
**Build Date:** October 28, 2025  
**Branch:** switch-controller-compatability  
**Firmware Sizes:**
- RP2040 (Pico): 321K
- RP2350 (Pico 2): 303K

**Build Command:**
```bash
./build-all.sh
# Outputs: dist/atari_ikbd_pico.uf2 and dist/atari_ikbd_pico2.uf2
```

## 🎮 Controller Configuration

The arcade stick works in both **Simple HID mode** (default) and **Full mode 0x30** (after Pro Controller init). Since it's a third-party controller, it uses Simple HID mode which is fully supported.

**Button Mapping (Atari ST):**
- D-Pad → Joystick Direction
- Any action button → Fire button
- Compatible with all Atari ST games expecting digital joystick

## 📝 Files Modified

### Core Implementation
- `include/switch_controller.h` - Added POWERA_FUSION_ARCADE_V2
- `src/switch_controller.c` - Updated detection, mount, and naming
- `src/hid_app_host.c` - Enhanced debugging (disabled in production)

### Documentation
- `README.md` - Added PowerA to supported controllers
- `include/version.h` - Bumped to 8.0.0
- `build-all.sh` - Updated version display
- `RELEASE_NOTES_v8.0.md` - Created
- `POWERA_ARCADE_IMPLEMENTATION.md` - This file

## 🚀 Deployment

**Status:** Ready for production  
**Tested:** ✅ Working on user's hardware  
**Debug Code:** Disabled in production build  
**Performance:** No degradation

Users can now simply flash the firmware and plug in their PowerA arcade stick - it will work immediately!

---

## 📌 Notes for Future Development

1. **Other PowerA Models:** If other PIDs are reported, add them similarly
2. **Debug Blocks:** All debug code preserved in `#if 0` blocks - easy to re-enable
3. **Version Tracking:** PowerA seems to use different PIDs for hardware revisions
4. **Mac USB Info:** Useful tool for getting device details when debugging

## ✨ Success!

The PowerA Fusion Wireless Arcade Stick is now **fully supported** in version 8.0.0! 🎉


