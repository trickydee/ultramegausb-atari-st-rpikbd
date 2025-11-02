# GameCube Adapter - Critical Discovery & Next Steps

**Date:** November 1, 2025  
**Current Version:** 10.1.5 (HID approach)  
**Status:** 🔍 ROOT CAUSE IDENTIFIED - Needs vendor/raw USB implementation

---

## 🎯 Critical Discovery

After analyzing reference implementations (`gc-x` and Linux kernel driver), we discovered:

### The GameCube Adapter is NOT a HID Device!

**It uses RAW USB interrupt endpoints:**
- **EP IN:**  0x81 (reads 37-byte reports)
- **EP OUT:** 0x02 (sends init/rumble commands)
- **NO HID class descriptor!**

This explains why `tuh_hid_receive_report()` never gets called - the adapter doesn't present itself as HID!

---

## 📋 Reference Code Findings

### From gc-x (Rust):
```rust
handle.write_interrupt(endpoint_out, &[0x13], Duration::from_millis(16))?;
```
- Init command: **0x13** (NOT 0x11!)
- Uses direct interrupt endpoint writes

### From Linux Driver:
```c
#define EP_IN  0x81
#define EP_OUT 0x02

unsigned char payload[1] = { 0x13 };
libusb_interrupt_transfer(handle, EP_OUT, payload, sizeof(payload), &bytes_transferred, 0);

libusb_interrupt_transfer(handle, EP_IN, payload, 37, &size, 0);
if (size != 37 || payload[0] != 0x21) continue;
```

### Report Format (37 bytes):
```
Byte 0:    0x21 (signal byte)
Bytes 1-9:  Port 1 controller (9 bytes)
Bytes 10-18: Port 2 controller (9 bytes)  
Bytes 19-27: Port 3 controller (9 bytes)
Bytes 28-36: Port 4 controller (9 bytes)
```

**Per-controller data (9 bytes):**
```
Byte 0: Status (powered, type)
Bytes 1-2: Buttons (16 bits)
Bytes 3-4: Analog Stick X, Y
Bytes 5-6: C-Stick X, Y
Bytes 7-8: L/R Triggers
```

---

## 🚨 Current Problem

**HID approach doesn't work because:**
1. Adapter has no HID descriptor (or non-standard one)
2. TinyUSB HID driver won't claim it
3. Reports never arrive at `tuh_hid_report_received_cb()`

**Vendor class approach had build errors:**
- TinyUSB vendor class API incompatibility
- `pipe_handle_t` type doesn't exist in this version
- Vendor class may use different API in SDK version

---

## 🔧 Solution Options

### Option 1: Direct usbh API (Recommended)
Use TinyUSB's low-level `usbh` API to:
1. Claim interface 0 manually
2. Open interrupt endpoints 0x81 and 0x02
3. Use `usbh_edpt_xfer()` for raw interrupt transfers
4. Handle callbacks manually

**Pros:**
- Most control
- Works with any TinyUSB version
- Matches reference implementations

**Cons:**
- More complex
- Need to handle USB enumeration details

### Option 2: Fix Vendor Class
Update `gamecube_vendor.c` to use correct TinyUSB vendor API for this SDK version.

**Pros:**
- Cleaner abstraction
- TinyUSB handles some details

**Cons:**
- API version-dependent
- Currently has compilation errors
- May not support interrupt endpoints properly

### Option 3: Patch TinyUSB HID Driver
Modify TinyUSB to force-claim GameCube adapter as HID even without proper descriptor.

**Pros:**
- Uses existing HID infrastructure

**Cons:**
- Hacky
- May break with SDK updates
- Still need to handle 37-byte raw format

---

## 📦 Current Build (v10.1.5)

**Status:** Compiles successfully  
**Firmware:** `dist/atari_ikbd_pico2.uf2` (329K)

**What it does:**
- Detects GameCube adapter (VID:057E PID:0337) ✅
- Shows "GC VID Check" screens ✅
- Mounts via HID class ✅
- **Does NOT receive reports** ❌ (because adapter isn't HID!)

**Debug features:**
- "RAW CB!" screen (shows if reports arrive - they won't)
- "GC MOUNT PATH" tracking
- "GC Ready! Inst:X setup OK"

---

## 🎯 Recommended Next Steps

### Immediate Test:
Flash v10.1.5 (`dist/atari_ikbd_pico2.uf2`) and confirm "RAW CB!" never appears when pressing buttons/sticks. This confirms adapter isn't sending HID reports.

### Implementation Path:
1. **Research:** Check TinyUSB usbh examples for raw endpoint usage
2. **Implement:** Create direct usbh-based driver:
   - Detect VID:057E PID:0337 in enumeration
   - Claim interface 0
   - Open endpoints 0x81/0x02
   - Send 0x13 init command via interrupt OUT
   - Read 37-byte reports via interrupt IN
   - Parse and forward to `gc_process_report()`

3. **Test:** Verify reports arrive and controllers work

---

## 📝 Files Modified

**Created:**
- `src/gamecube_vendor.c` (disabled - has build errors)
- This status document

**Modified:**
- `include/tusb_config.h` - Attempted to enable vendor class (reverted)
- `CMakeLists.txt` - Added vendor source (commented out)
- `include/version.h` - v10.1.5
- `src/hid_app_host.c` - Enhanced debug for report arrival

---

## 💡 Key Insights

1. **Init command must be 0x13**, not 0x11 (0x11 is rumble)
2. **37-byte reports** with 0x21 signal byte
3. **Raw interrupt endpoints**, not HID reports
4. **PC mode required** (switch on adapter)
5. **Multiple interfaces** (4 total, one per controller port)

---

**Next Action:** Decide on implementation approach and proceed with direct usbh API or vendor class fix.



