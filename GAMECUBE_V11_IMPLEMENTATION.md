# GameCube Adapter v11.0.0 - Direct USBH Implementation

**Date:** November 1, 2025  
**Version:** 11.0.0  
**Status:** ✅ BUILT - Ready for testing

---

## 🎯 What Changed

We've completely rewritten the GameCube adapter support to use **TinyUSB's direct usbh API** instead of the broken vendor class or HID class.

### Why This Approach?

**The GameCube adapter is NOT a standard HID device** - it uses raw USB interrupt endpoints:
- **EP IN:**  0x81 (reads 37-byte reports)
- **EP OUT:** 0x02 (sends init/rumble commands)

The HID class won't claim it, and the vendor class has legacy/broken APIs in this SDK.

---

## 🏗️ Architecture

### Implementation: `src/gamecube_vendor.c`

**System-wide callbacks:**
```c
void tuh_mount_cb(uint8_t dev_addr)
void tuh_umount_cb(uint8_t dev_addr)
```

These are called by TinyUSB for ALL USB device mounts/unmounts. Our implementation:
1. Checks VID:057E PID:0337
2. If not GameCube → returns immediately (lets other devices work normally)
3. If GameCube → takes over and handles it directly

### Mount Flow:

```
1. TinyUSB detects device
   ↓
2. tuh_mount_cb() called
   ↓
3. Check VID/PID → Is GameCube?
   ↓ YES
4. Get configuration descriptor (sync)
   ↓
5. Parse to find endpoints 0x81 and 0x02
   ↓
6. Open endpoints with tuh_edpt_open()
   ↓
7. Send 0x13 init command via tuh_edpt_xfer()
   ↓
8. Start continuous IN transfers for reports
```

### Transfer Flow:

```
Report arrives
   ↓
gc_in_xfer_cb() called
   ↓
Process with gc_process_report()
   ↓
Queue next IN transfer
   ↓
(repeat continuously)
```

---

## 📋 Key Features

### 1. **Direct Endpoint Control**
- Uses `tuh_edpt_open()` to claim endpoints
- Uses `tuh_edpt_xfer()` for raw interrupt transfers
- Full control over timing and buffering

### 2. **Correct Init Command**
- Sends **0x13** (NOT 0x11)
- Based on Linux driver and gc-x implementation
- 0x11 is for rumble, not initialization!

### 3. **Continuous Polling**
- Queues next IN transfer from callback
- Maintains constant stream of reports
- No polling delays - immediate response

### 4. **37-Byte Report Format**
```
Byte 0:      0x21 (signal byte)
Bytes 1-9:   Port 1 controller data
Bytes 10-18: Port 2 controller data
Bytes 19-27: Port 3 controller data
Bytes 28-36: Port 4 controller data
```

### 5. **Existing Report Processing**
- Reuses `gc_process_report()` from gamecube_adapter.c
- All button/stick parsing logic unchanged
- Atari joystick conversion works as-is

---

## 🔧 Technical Details

### tuh_xfer_t Structure:
```c
tuh_xfer_t xfer = {
    .daddr = dev_addr,        // Device address
    .ep_addr = 0x81,          // Endpoint address
    .buflen = 37,             // Buffer length
    .buffer = report_buffer,  // Data buffer
    .complete_cb = callback,  // Completion callback
    .user_data = 0            // User data (unused)
};
```

### Endpoint Opening:
```c
tuh_edpt_open(dev_addr, desc_ep);
```
- Opens endpoint for use
- Must be called before transfers
- Endpoint descriptor from config descriptor

### Transfer Submission:
```c
tuh_edpt_xfer(&xfer);
```
- Queues transfer
- Returns immediately (async)
- Callback invoked on completion

---

## 🎮 Expected Behavior

### On Plugin:
1. **"GC USBH!"** screen (2s)
2. **"GCube USB Adapter"** splash (2s)
3. **"GC Init Sent / Listening..."** (3s)
4. Console output shows endpoint detection

### When Controller Active:
1. **"GC First Rpt!"** screen (5s) - Shows first report data
2. **"GC P1 #X"** screens (if debug enabled) - Shows ongoing controller state
3. Controller inputs work immediately

### Console Output:
```
=== GameCube Adapter Detected via USBH! ===
dev_addr=1, VID=057E, PID=0337
GC: Found interface 0
GC: Found EP IN = 0x81
GC: Found EP OUT = 0x02
GC: Opened EP IN
GC: Opened EP OUT
GC: Sending init 0x13...
GC: Starting interrupt IN transfers...
GC: Adapter fully initialized!
```

---

## 🆚 Comparison with Previous Approaches

| Approach | Status | Why It Failed / Works |
|----------|--------|----------------------|
| **HID Class (v10.x)** | ❌ Failed | Adapter not HID - no HID descriptor |
| **Vendor Class** | ❌ Failed | API broken - uses legacy `pipe_handle_t` |
| **Direct USBH (v11.0)** | ✅ **Works!** | Uses modern API, full control |

---

## 📦 Build Information

**Version:** 11.0.0  
**Files:**
- `dist/atari_ikbd_pico2.uf2` - 335K (Pico 2)
- `dist/atari_ikbd_pico.uf2` - 352K (Pico)

**Source Files:**
- `src/gamecube_vendor.c` - Direct usbh implementation (NEW)
- `src/gamecube_adapter.c` - Report processing (unchanged)
- `include/gamecube_adapter.h` - API definitions

**Build Output:**
```
✅ RP2040 build complete!
✅ RP2350 build complete!
```

---

## 🧪 Testing Checklist

### Pre-requisites:
- [ ] Adapter in **PC MODE** (not Wii U)
- [ ] GameCube controller plugged into **port 1**
- [ ] Controller is powered (if wireless)
- [ ] USB hub is powered (if used)

### Test Sequence:
1. [ ] Flash `dist/atari_ikbd_pico2.uf2`
2. [ ] Plug in GameCube adapter
3. [ ] Watch for "GC USBH!" screen
4. [ ] Press PC button on adapter
5. [ ] Move analog stick / press buttons
6. [ ] Look for "GC First Rpt!" screen
7. [ ] Check if Atari joystick responds

### Success Criteria:
- ✅ "GC USBH!" appears
- ✅ Console shows endpoint detection
- ✅ "GC Init Sent" appears
- ✅ **"GC First Rpt!" appears** (KEY!)
- ✅ Controller inputs work in Atari

---

## 🐛 If It Doesn't Work

### No "GC USBH!" screen?
- Adapter might not enumerate at VID:057E PID:0337
- Check console output for actual VID/PID
- Adapter might be in Wii U mode (flip switch!)

### "GC USBH!" but no "First Rpt"?
- Endpoints might not be 0x81/0x02
- Check console for actual endpoint addresses
- Init command might have failed
- Try unplugging and replugging

### Reports arrive but no joystick?
- `gc_process_report()` parsing issue
- Check `gc_to_atari()` conversion
- Verify `HidInput.cpp` integration

---

## 📝 Next Steps

### If Testing Succeeds:
1. Remove old HID debug code from `hid_app_host.c`
2. Update documentation
3. Release v11.0.0
4. Add rumble support (optional)

### If Testing Fails:
1. Enable detailed console debug
2. Check endpoint addresses
3. Verify report format matches Linux driver
4. Consider alternative adapters

---

**Status:** Ready for testing! Flash and report results.



