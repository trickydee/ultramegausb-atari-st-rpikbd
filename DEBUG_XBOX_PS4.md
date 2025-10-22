# Xbox/PS4 Controller Reconnection Debug Guide

## Issue
Xbox controller works when first connected, but fails to work after PS4 controller has been used.

## Enhanced Debug Firmware
Version 7.1.0 now includes extensive debug logging to diagnose this issue.

---

## How to Collect Debug Output

### 1. Connect Serial Console

**On Mac:**
```bash
screen /dev/tty.usbmodem* 115200
```

**On Linux:**
```bash
screen /dev/ttyACM0 115200
```

**To exit screen:** Press `Ctrl+A` then `K` then `Y`

### 2. Test Sequence

Follow this exact sequence and capture ALL console output:

#### Test 1: Xbox First (Baseline - Should Work)
```
1. Flash firmware
2. Power on
3. Plug in Xbox controller
4. Wait for "XINPUT MOUNT CALLBACK" message
5. Test buttons/analog stick
6. Note: Does it work? ✅ or ❌
7. Unplug Xbox controller
8. Wait for "XINPUT UNMOUNTED" message
```

#### Test 2: Xbox After Xbox (Should Work)
```
9. Plug in Xbox controller again
10. Wait for "XINPUT MOUNT CALLBACK" message
11. Test buttons/analog stick  
12. Note: Does it work? ✅ or ❌
13. Unplug Xbox controller
```

#### Test 3: PS4 Usage (Control Test)
```
14. Plug in PS4 controller
15. Wait for mount message
16. Test buttons/analog stick
17. Note: Does PS4 work? ✅ or ❌
18. Unplug PS4 controller
```

#### Test 4: Xbox After PS4 (THE PROBLEM)
```
19. Plug in Xbox controller
20. CRITICAL: Watch for "XINPUT MOUNT CALLBACK" message
21. Test buttons/analog stick
22. Note: Does it work? ✅ or ❌

If it doesn't work, look at the console output!
```

---

## What to Look For in Debug Output

### Normal Xbox Mount (Should See This)

```
========================================
XINPUT MOUNT CALLBACK
========================================
  dev_addr:   1
  instance:   0
  connected:  1
  itf_num:    0
  ep_in:      0x81
  ep_out:     0x01
  pointer:    0x20001234
  Type:       Xbox One
========================================

XINPUT: Register request for address 1, pointer=0x20001234
XINPUT: Registered controller at address 1, connected=1
```

### What If Mount Callback Never Appears?

If you plug in Xbox after PS4 and you **DON'T SEE** the "XINPUT MOUNT CALLBACK" box, then:

**Problem:** TinyUSB's xinput driver isn't even detecting the Xbox controller!

**Possible causes:**
1. TinyUSB driver got into a bad state after PS4
2. USB enumeration is failing
3. The controller is being detected as a different device type

**Look for:** Any other USB device mount messages instead

### What If Mount Callback Appears But Connected=0?

```
  connected:  0  ← THIS IS THE PROBLEM
```

**Problem:** TinyUSB detected the controller but it's not marked as connected

**Possible causes:**
1. XInput initialization sequence failed
2. Controller needs special wake-up for Xbox One
3. Endpoint communication issue

### What If Pointer is NULL or Invalid?

```
XINPUT: Register request for address 1, pointer=0x00000000
```

**Problem:** TinyUSB is passing a NULL pointer

**This would cause a crash** - unlikely to be the issue

### What If Address Conflicts?

```
XINPUT: Clearing stale entry at address 1 (same pointer)
XINPUT: Replacing existing controller at address 1 (old ptr=0x20001234)
```

**This is GOOD** - means our fix is detecting and clearing conflicts

---

## Key Debug Messages

### Registration Messages

| Message | Meaning |
|---------|---------|
| `XINPUT: Register request for address X, pointer=0xYYYY` | Starting registration |
| `XINPUT: Clearing stale entry at address X` | Found old entry, clearing it |
| `XINPUT: Replacing existing controller at address X` | Overwriting existing entry |
| `XINPUT: Registered controller at address X, connected=Y` | Registration complete |

### Unregistration Messages

| Message | Meaning |
|---------|---------|
| `XINPUT: Unregister request for address X` | Starting unregistration |
| `XINPUT: Also clearing stale entry at address X` | Clearing duplicate pointers |
| `XINPUT: Unregistered controller at address X` | Unregistration complete |

---

## Common Failure Patterns

### Pattern 1: Mount Callback Not Called

**Symptoms:**
- Xbox works first time
- After PS4, plugging Xbox shows no "XINPUT MOUNT CALLBACK"
- No registration messages

**Diagnosis:** TinyUSB driver is not recognizing the device

**Possible fix:** Need to reset TinyUSB state when PS4 unmounts

### Pattern 2: Mount Called But connected=0

**Symptoms:**
- "XINPUT MOUNT CALLBACK" appears
- `connected: 0` shown
- Registration completes but controller doesn't work

**Diagnosis:** XInput initialization failed or controller not ready

**Possible fix:** Need to handle Xbox One wireless controller initialization differently

### Pattern 3: Wrong Address

**Symptoms:**
- Controller registers at address different from expected
- Old address still has data

**Diagnosis:** USB address reuse or TinyUSB state confusion

**Possible fix:** More aggressive clearing of all addresses

---

## Next Steps Based on Results

### If Mount Callback Never Appears:

We need to investigate TinyUSB's device enumeration. The issue is in the USB stack itself, not our code.

**Potential solutions:**
1. Force USB re-enumeration after PS4 unmount
2. Reset xinput driver state
3. Check if PS4 is leaving USB in a bad state

### If Connected=0:

We need to check Xbox One controller initialization sequence.

**Potential solutions:**
1. Send proper init packet for Xbox One controllers
2. Wait for controller ready before marking as connected
3. Check endpoint initialization

### If Address Conflicts:

Our current fix should handle this, but we might need to be more aggressive.

**Potential solutions:**
1. Clear ALL addresses when any controller unmounts
2. Validate pointers before using them
3. Add timeout-based stale pointer detection

---

## Code Locations for Further Investigation

If we need to dig deeper:

### TinyUSB XInput Driver
- `pico-sdk/lib/tinyusb/src/class/xinput/xinput_host.c`
- Look at `xinputh_init()`, `xinputh_open()`, `xinputh_xfer_cb()`

### Our Xbox Integration
- `src/main.cpp` - Mount/unmount callbacks
- `src/xinput_atari.cpp` - Controller registration and usage
- `src/xinput_host.c` - Official xinput driver (if we copied it)

### USB Device Management
- `src/hid_app_host.c` - HID device handling (PS4 goes through here)
- May need to add cleanup when HID devices unmount

---

## Temporary Workaround

Until we fix this properly, you can:

1. **Use only Xbox controllers** (no PS4)
2. **Use only PS4 controllers** (no Xbox)
3. **Power cycle the Pico** after using PS4 before using Xbox
4. **Use Xbox first, then PS4** (PS4 works fine after Xbox)

---

## Please Collect This Information

When you test the new firmware (v7.1.0 in `dist/`), please capture:

1. **Full serial console output** from power-on through the full test sequence
2. **Exact controller models** (Xbox One, Xbox Series X, PS4 DualShock 4, etc.)
3. **Which test cases work** and which fail
4. **Any error messages** or unexpected output

Save the console output to a file so we can analyze it together!

---

**Firmware:** v7.1.0 with enhanced Xbox/PS4 debug logging  
**Files:** `dist/atari_ikbd_pico.uf2` or `dist/atari_ikbd_pico2.uf2`  
**Date:** October 21, 2025

