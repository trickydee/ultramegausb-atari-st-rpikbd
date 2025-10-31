# GameCube Controller USB Adapter - Debug Status

**Date:** October 31, 2025  
**Branch:** gamecube-controller-usb-adapter-support  
**Current Version:** 10.1.1  
**Status:** 🔍 DEBUGGING - Detection works, reports not arriving

---

## 🎯 Current Situation

### ✅ What's Working:
1. **GameCube adapter is detected** - VID:057E PID:0337 Match:1
2. **Splash screen shows** - "GCube USB Adapter" displays
3. **Joystick counter increments** - Confirms device is active
4. **Multiple instances detected:**
   - Before PC button: Instance 0, Addr 1
   - After PC button: Instance 2, Addr 3

### ❌ What's NOT Working:
1. **Reports not reaching `gc_process_report()`** - Debug screens don't show
2. **Controller inputs don't work** - Buttons/sticks have no effect
3. **"GC First Rpt!" never appears** - Indicates reports aren't being processed

---

## 🔍 Key Findings

### GameCube Adapter Hardware Info:
**From Mac System Info:**
```
Product: "GameCube For Switch"
Vendor: "Nintendo"
VID: 0x057E (1406 decimal)
PID: 0x0337 (823 decimal)
Serial: "GH-GC-001 T13B"
```

**Adapter Type:**
- Third-party GameCube USB adapter
- Has WII U / PC mode switch
- **PC mode** is required (standard HID)
- Supports up to 4 GameCube controllers

### USB Behavior:
- **Multiple USB interfaces** (at least 3: instance 0, 1, 2)
- **Instance 0** mounts first (addr 1)
- **Instance 2** mounts after pressing PC button (addr 3)
- **Mouse counter increments** when PC button pressed = reports ARE being received, but going to generic HID path

---

## 📋 Implementation Status

### Files Created:
- ✅ `include/gamecube_adapter.h` - Complete API definitions
- ✅ `src/gamecube_adapter.c` - Full implementation with debug
- ✅ `GAMECUBE_IMPLEMENTATION.md` - Planning document

### Files Modified:
- ✅ `src/hid_app_host.c` - Added GC detection, mount, report processing, unmount
- ✅ `src/HidInput.cpp` - Added `get_gamecube_joystick()` function
- ✅ `include/HidInput.h` - Added function declaration
- ✅ `CMakeLists.txt` - Added gamecube_adapter.c to build
- ✅ `build-all.sh` - Fixed submodule init, updated version
- ✅ `include/version.h` - Bumped to 10.1.1

### Integration Points:
1. **Mount Callback** (line ~239 in hid_app_host.c):
   - Detects VID:057E PID:0337
   - Allocates device for each instance
   - Calls `gc_mount_cb()` on instance 0 only
   - Calls `tuh_hid_receive_report()` on each instance
   
2. **Report Callback** (line ~694 in hid_app_host.c):
   - Checks `gc_is_adapter(vid, pid)`
   - Calls `gc_process_report()`
   - Queues next report
   
3. **Unmount Callback** (line ~584 in hid_app_host.c):
   - Checks `gc_is_adapter(vid, pid)`
   - Calls `gc_unmount_cb()`

---

## 🐛 Current Problem: Reports Not Reaching gc_process_report()

### Theory:
The GameCube adapter has **4 USB HID interfaces** (one per controller port). When mounted:
- **Instance 0** is detected correctly as GameCube ✅
- **Instances 1-3** might also be mounting...
- **Instance 2** sends reports when PC button is pressed
- But reports from instance 2 might be going to **generic HID path** instead of GameCube path

### Evidence:
1. Mouse counter increments = generic HID is processing something
2. No "GC RPT CALLBACK!" appears = `gc_process_report()` not being called
3. Multiple instances seen (0 and 2)
4. Different addresses (1 and 3) suggest re-enumeration or multiple devices

### What We Added (Latest Changes):
1. **Multi-instance support** - Now handles all instances, not just 0
2. **Enhanced VID/PID debug** - Shows instance, address, protocol on OLED
3. **Report callback debug** - Should show "GC RPT CALLBACK!" when reports arrive
4. **Initialization command** - Sends 0x11 to activate adapter (like reference driver)

---

## 🛠️ GameCube Report Format (From Reference Driver)

### Report Structure (37 bytes):
```
Byte 0: 0x21 (signal byte)
Bytes 1-9: Port 1 controller (9 bytes)
Bytes 10-18: Port 2 controller (9 bytes)
Bytes 19-27: Port 3 controller (9 bytes)
Bytes 28-36: Port 4 controller (9 bytes)
```

### Per-Controller Data (9 bytes):
```
Byte 0: Status (powered:1bit, type:2bits)
Byte 1: Buttons (a, b, x, y, d_left, d_right, d_down, d_up)
Byte 2: Buttons (start, z, r, l)
Byte 3: Analog Stick X (0-255, 127=center)
Byte 4: Analog Stick Y (0-255, 127=center, inverted)
Byte 5: C-Stick X
Byte 6: C-Stick Y
Byte 7: L Trigger (analog 0-255)
Byte 8: R Trigger (analog 0-255)
```

### Initialization Required:
```c
// Send output report 0x11 with 5 bytes
{0x11, 0x00, 0x00, 0x00, 0x00}  // Enable adapter, rumble off for all ports
```

---

## 🎯 Next Steps to Debug

### Option A: Check if reports go to different instance
The "PC button" might activate instance 2, which might not be detected as GameCube.

**Test:** With v10.1.1, you should see "GC VID Check" for BOTH instances. If instance 2 shows Match:0, that's the problem.

### Option B: Reports might be going to generic HID path
If instance 2 is being parsed as generic HID (mouse), we need to ensure ALL instances are caught.

**Solution:** May need to track GameCube by device address, not instance.

### Option C: Initialization might need to be on specific instance
Maybe the init command needs to go to instance 2 instead of 0?

**Test:** Try sending init to all instances.

---

## 📝 Reference Code Insights

### From GCN_Adapter-Driver:
- **Rumble command structure:** First byte 0x11, then 4 bytes for rumble state
- **Report size:** Exactly 37 bytes
- **Signal byte:** Always 0x21 at start of report
- **Default deadzone:** 35 (out of 255)
- **Y-axis inversion:** Reference driver inverts Y axis (`~Y` operation)

### USB Characteristics:
- **Interrupt endpoint** for reading reports
- **Continuous reader** required
- **No special pairing** needed in PC mode
- **Rumble support** optional (we don't need it for Atari)

---

## 🔧 Current Code State

### Detection Function (gamecube_adapter.h):
```c
#define GAMECUBE_VENDOR_ID   0x057E
#define GAMECUBE_ADAPTER_PID 0x0337

bool gc_is_adapter(uint16_t vid, uint16_t pid) {
    if (vid != GAMECUBE_VENDOR_ID) return false;
    return (pid == GAMECUBE_ADAPTER_PID);
}
```

### Mount Logic (hid_app_host.c ~line 263):
- Detects ALL instances with matching VID/PID
- Shows splash only on instance 0
- Sends init command only on instance 0
- Calls `tuh_hid_receive_report()` on each instance

### Report Processing (hid_app_host.c ~line 694):
- Checks VID/PID match
- Should process reports from any instance
- Should show "GC RPT CALLBACK!" on OLED

---

## 🎮 Hardware Configuration Checklist

When testing, ensure:
- ✅ Adapter switch is in **PC MODE** (not Wii U)
- ✅ GameCube controller is **plugged into port 1** of adapter
- ✅ Controller is **powered on** (if wireless/battery-powered)
- ✅ Adapter has **sufficient power** (USB hub powered?)
- ⚠️ Try **pressing PC button** after adapter is connected

---

## 💡 Debugging Commands Added

Current firmware (v10.1.1) has extensive debug:
1. **"GC VID Check"** - Shows VID/PID match for Nintendo devices
2. **"GC Init Sent"** - Confirms initialization sent
3. **"GC RPT CALLBACK!"** - Should show when reports arrive (NOT SHOWING = PROBLEM)
4. **"GC First Rpt!"** - First report with data
5. **"GC Scan"** or **"GC P1"** - Ongoing controller status

---

## 🚨 Current Blocker

**Reports are being received by TinyUSB** (mouse counter increments) but **NOT reaching gc_process_report()**.

**Most likely cause:** Instance 2 (the one that sends reports in PC mode) is being processed as **generic HID mouse** before the GameCube check happens.

**Next action needed:** 
1. Flash v10.1.1
2. Note how many "GC VID Check" screens appear
3. Check if instance 2 shows Match:1 or Match:0
4. Press PC button and see if "GC RPT CALLBACK!" ever appears

---

## 📦 Build Information

**Current firmware:**
- Branch: `gamecube-controller-usb-adapter-support`
- Version: 10.1.1
- RP2040: 344KB
- RP2350: 327KB
- Debug: Enabled
- Location: `dist/atari_ikbd_pico.uf2` and `dist/atari_ikbd_pico2.uf2`

**Compiled successfully:** Yes ✅

---

**Status:** Waiting for user to test v10.1.1 and report what mount events appear on OLED.

