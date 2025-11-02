# GameCube Adapter - Final Status & Solution Plan

**Date:** November 1, 2025  
**Current Version:** 11.0.6  
**Status:** 🔍 CALLBACKS NOT FIRING - Need class driver approach

---

## ✅ What Works (v11.0.6)

1. **Detection** - Adapter detected correctly (VID:057E PID:0337)
2. **HID Skip** - HID driver properly skips the adapter
3. **USBH Mount** - Our `tuh_mount_cb()` is called
4. **Endpoint Opening** - Both endpoints (0x81 IN, 0x02 OUT) open successfully
5. **Transfer Queueing** - Both OUT (init) and IN (report) transfers queue successfully
6. **Joystick Counter** - Increments to 1 using `gc_notify_mount()`
7. **Integration** - `HidInput.cpp` checks `gc_get_adapter_vendor()`

## ❌ What Doesn't Work

**Transfer callbacks NEVER fire:**
- `gc_in_xfer_cb()` - Never called (IN:0)
- `gc_out_xfer_cb()` - Never called (OUT:0)
- No "INIT OK!" screen
- No "CB CALLED!" screen
- No reports processed

---

## 🔍 Root Cause Analysis

### Reference Driver Comparison:

| Driver | Language | Transfer Method | Works? |
|--------|----------|-----------------|--------|
| Linux | C | `libusb_interrupt_transfer()` - **blocking** | ✅ |
| gc-x | Rust | `read_interrupt()` - **blocking** | ✅ |
| OSX | Objective-C | `libusb_fill_bulk_transfer()` - **async** | ✅ |
| Delfinovin | C# | `Read()` - **blocking** | ✅ |

**Pattern:** 3 out of 4 use **synchronous/blocking** reads in a poll loop!

### TinyUSB Architecture Issue:

TinyUSB's host stack is **class-driver based**:

1. **During enumeration:**
   - Class drivers register via `xxx_open()` function
   - Class driver claims interface and opens endpoints
   - TinyUSB records which driver owns which endpoints

2. **During transfers:**
   - `tuh_edpt_xfer()` queues transfer
   - When complete, TinyUSB looks up which driver owns the endpoint
   - Calls that driver's `xxx_xfer_cb()` function

3. **Our problem:**
   - We manually open endpoints without class driver registration
   - TinyUSB doesn't know who owns them
   - Transfer completes but no callback is invoked
   - **Callbacks go nowhere!**

---

## 📋 Attempted Solutions

### v10.x - HID Class Approach
**Status:** ❌ Failed  
**Reason:** GameCube adapter isn't HID - uses raw endpoints

### v11.0.0-11.0.2 - Vendor Class
**Status:** ❌ Failed  
**Reason:** TinyUSB vendor class has legacy/broken API (`pipe_handle_t` doesn't exist)

### v11.0.3-11.0.6 - Direct USBH with Callbacks
**Status:** ⚠️ Partial  
**Achievements:**
- Endpoints open ✅
- Transfers queue ✅
- Joystick counter works ✅
**Problem:**
- Callbacks never fire ❌
- No way to receive data ❌

---

## 🎯 Solution Options

### Option 1: Implement Proper TinyUSB Class Driver (Recommended)

Create `gamecube_driver.c` with full class driver structure:

```c
// Driver registration structure
usbh_class_driver_t const gamecube_driver = {
  .name       = "GAMECUBE",
  .init       = gamecubeh_init,
  .deinit     = gamecubeh_deinit,
  .open       = gamecubeh_open,      // Called during enumeration
  .set_config = gamecubeh_set_config,
  .xfer_cb    = gamecubeh_xfer_cb,   // Transfer callback - THIS IS KEY!
  .close      = gamecubeh_close
};
```

**Pros:**
- Proper TinyUSB integration
- Callbacks will work
- Clean architecture

**Cons:**
- Need to register driver in tusb.c
- More complex implementation
- Requires modifying TinyUSB source

### Option 2: Synchronous Polling (Alternative)

Implement blocking reads like Linux driver:

```c
void gc_poll_sync(void) {
    if (!gc_device) return;
    
    // Blocking read (if TinyUSB supports it)
    uint8_t buffer[37];
    // Read from endpoint - blocks until data arrives
    // Process immediately
    gc_process_report(dev_addr, buffer, 37);
}
```

**Pros:**
- Simpler than class driver
- Matches most reference implementations

**Cons:**
- Need to find if TinyUSB has sync transfer API
- Might block main loop (need separate thread)
- May not be supported

### Option 3: Register Fake Class Driver

Minimal driver that just routes callbacks:

```c
// Minimal driver just for callback routing
static bool fake_open(uint8_t rhport, uint8_t dev_addr, 
                      tusb_desc_interface_t const* desc, uint16_t max_len) {
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    return (vid == 0x057E && pid == 0x0337);
}

static bool fake_xfer_cb(uint8_t dev_addr, uint8_t ep_addr, 
                         xfer_result_t result, uint32_t xferred_bytes) {
    // Route to our real callback
    if (ep_addr == 0x81) {
        // Handle IN
    } else if (ep_addr == 0x02) {
        // Handle OUT  
    }
    return true;
}
```

**Pros:**
- Smaller modification than full driver
- Callbacks work

**Cons:**
- Still requires TinyUSB source modification
- Hacky

---

## 📊 Test Results Summary

### v11.0.6 Test Results:
```
✅ ">>> USBH / >>> MOUNT" appears
✅ "Set config" appears
✅ "Open EPs / Try INTERRUPT" appears
✅ "EPs OK! / IN+OUT opened" appears
✅ "GCube USB Adapter" appears
✅ "CHK 1: / Past listen" appears
✅ "QUEUED! / IN xfer ready" appears
✅ "SETUP OK! / Joy counter++" appears
✅ Joystick counter = 1 (working!)
❌ "NO CB! / IN:0 OUT:0" appears (problem!)
❌ No "INIT OK!" (OUT callback never fires)
❌ No "CB CALLED!" (IN callback never fires)
```

**Conclusion:** Mount succeeds completely, but **TinyUSB doesn't invoke transfer callbacks** for manually-opened endpoints.

---

## 🛠️ Recommended Next Step

**Implement Option 1: Proper TinyUSB Class Driver**

This requires:
1. Create `src/gamecube_driver.c` with driver structure
2. Register in `pico-sdk/lib/tinyusb/src/tusb.c` driver list
3. Create patch for registration
4. Implement driver callbacks

**Estimated effort:** 2-3 hours  
**Success probability:** 95%

---

## 💡 Alternative: Try Hijacking HID

Instead of patching HID to skip GameCube, what if we:
1. Let HID claim it
2. In `tuh_hid_mount_cb()`, detect it's GameCube
3. Send our 0x13 init command
4. Process reports in `tuh_hid_report_received_cb()`

This might work even though adapter isn't "proper" HID, since HID driver successfully claims the endpoints!

---

**Current Status:** v11.0.6 proves endpoints work, but callbacks are the blocker. Need class driver or HID hijacking approach.



