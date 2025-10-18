# USB Debugging Methodology for Embedded Systems (No Console Access)

## Context
This document captures the successful debugging methodology used to diagnose and fix Logitech Unifying receiver support in the Atari ST USB adapter project (October 2025). All debugging was done via OLED display with no console/serial access.

## The Challenge
**Problem:** Logitech Unifying wireless mouse not detected
**Constraint:** No serial console access - all debugging via 128x64 OLED display
**Complexity:** Multi-interface USB device sharing same address for keyboard and mouse

## Systematic Debugging Approach

### Phase 1: Enumerate the Problem Space
**Goal:** Confirm device is detected at USB level

**OLED Display Added:**
```c
// In tuh_hid_mount_cb() - show EVERY device enumeration
ssd1306_clear(&disp);
ssd1306_draw_string(&disp, 5, 0, 2, (char*)"HID!!");
snprintf(line, sizeof(line), "Addr:%d Inst:%d", dev_addr, instance);
ssd1306_draw_string(&disp, 5, 25, 1, line);
snprintf(line, sizeof(line), "VID:%04X", vid);
ssd1306_draw_string(&disp, 5, 40, 1, line);
snprintf(line, sizeof(line), "P:%d L:%d", protocol, desc_len);
ssd1306_draw_string(&disp, 5, 55, 1, line);
ssd1306_show(&disp);
sleep_ms(3000);
```

**Key Findings:**
- Logitech Unifying (VID 046D, PID C54D) enumerates 3 interfaces
- Instance 1: Protocol 2 (Boot Mouse), 158-byte descriptor
- Instance 2: Protocol 0, 54-byte descriptor  
- Instance 3: Protocol 0, 0-byte descriptor
- Multiple interfaces share same USB address (1)

**Critical Insight:** Multi-interface device = multiple HID interfaces on ONE USB address

### Phase 2: Trace Parsing Pipeline
**Goal:** Verify device is correctly parsed as MOUSE

**OLED Display Added:**
```c
// After USB_ProcessHIDReport() - show parser result
if (vid == 0x046D) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 20, 10, 2, (char*)"PARSED");
    const char* type_str = (filter_type == HID_MOUSE) ? "MOUSE" : "UNKNOWN";
    snprintf(line, sizeof(line), "Type: %s", type_str);
    ssd1306_draw_string(&disp, 5, 35, 1, line);
    ssd1306_show(&disp);
    sleep_ms(2000);
}
```

**Key Findings:**
- Mouse correctly parsed as HID_MOUSE
- HID parser successfully identified device type
- No issue with USB-level detection

**Critical Insight:** Detection working, issue must be in callback/registration layer

### Phase 3: Track Callback Invocation
**Goal:** Verify mounted callback is being called

**OLED Display Added:**
```c
// Before calling tuh_hid_mounted_cb() - confirm code path is reached
if (vid == 0x046D) {
    ssd1306_draw_string(&disp, 10, 0, 1, (char*)"CALLING CB");
    snprintf(line, sizeof(line), "Type: %s", 
             filter_type == HID_MOUSE ? "MOUSE" : "KEYBOARD");
    ssd1306_show(&disp);
    sleep_ms(2000);
}
```

**Key Findings:**
- Callback code path WAS reached for keyboard (inst 0)
- Callback code path NOT reached for mouse (inst 1)
- Boot protocol mouse handler only called callback for "first interface"

**Critical Insight:** Multi-interface suppression logic prevented mouse callback

### Phase 4: Verify Registration
**Goal:** Check if devices are in the application device map

**OLED Display Added:**
```c
// In handle_mouse() - show what's actually registered
static uint32_t debug_count = 0;
if ((debug_count++ % 500) == 0) {
    ssd1306_draw_string(&disp, 10, 0, 1, (char*)"DEVICE MAP");
    snprintf(line, sizeof(line), "Devices: %d", (int)device.size());
    for (auto it : device) {
        HID_TYPE tp = tuh_hid_get_type(it.first);
        const char* type = (tp == HID_MOUSE) ? "M" : "K";
        snprintf(line, sizeof(line), "Addr %d: %s", it.first, type);
    }
    ssd1306_show(&disp);
}
```

**Key Findings:**
- Only keyboard (Addr 1: K) in map
- Mouse completely absent
- Confirmed callback wasn't being called for mouse

**Critical Insight:** Mouse never registered due to callback suppression

### Phase 5: Test Data Flow
**Goal:** Verify reports are being received at USB level

**OLED Display Added:**
```c
// In tuh_hid_report_received_cb() - show report activity
if (vid == 0x046D) {
    static uint32_t report_count = 0;
    if ((report_count++ % 100) == 0) {
        ssd1306_draw_string(&disp, 15, 0, 1, (char*)"LOGITECH DATA");
        snprintf(line, sizeof(line), "Type: %s", 
                 dev->hid_type == HID_MOUSE ? "Mouse" : "Keyboard");
        snprintf(line, sizeof(line), "Reports: %lu", report_count);
        snprintf(line, sizeof(line), "Len:%d A:%d I:%d", len, dev_addr, instance);
        ssd1306_show(&disp);
    }
}
```

**Key Findings:**
- Reports WERE flowing from mouse (180+ reports)
- Type: Mouse, Len:4, Address:1, Instance:1
- USB communication working perfectly

**Critical Insight:** Problem isolated to application registration/callback layer, NOT USB

### Phase 6: Fix and Verify
**Goal:** Implement fix and confirm callback is called

After fixing callback to always trigger for mice, added:
```c
// In tuh_hid_mounted_cb() - show registration
ssd1306_draw_string(&disp, 10, 0, 1, (char*)"MOUNT CALLBACK");
snprintf(line, sizeof(line), "A:%d T:%s", dev_addr, 
         tp == HID_MOUSE ? "MOUSE" : "KEYBOARD");
snprintf(line, sizeof(line), "Key:%d", device_key);
ssd1306_show(&disp);
sleep_ms(2000);
```

**Key Findings:**
- Callback NOW called twice (keyboard + mouse)
- Mouse registered at key 129 (offset addressing)
- Both devices in map

**Critical Insight:** Offset addressing scheme required for same-address devices

### Phase 7: Verify Handler Processing
**Goal:** Confirm mouse handler finds and processes mouse

**OLED Display Added:**
```c
// In handle_mouse() - show handler state
if ((count % 500) == 0) {
    ssd1306_draw_string(&disp, 15, 0, 1, (char*)"MOUSE HANDLER");
    snprintf(line, sizeof(line), "Key:%d Addr:%d", it.first, actual_addr);
    bool mounted = tuh_hid_is_mounted(it.first);
    bool busy = tuh_hid_is_busy(it.first);
    snprintf(line, sizeof(line), "M:%d B:%d", mounted, busy);
    HID_ReportInfo_t* info = tuh_hid_get_report_info(it.first);
    snprintf(line, sizeof(line), "Info: %s", info ? "YES" : "NULL");
    ssd1306_show(&disp);
}
```

**Key Findings:**
- Mouse found in device map (Key:129)
- Initially showed B:1 (always busy) - blocking processing
- After fixing to use device key instead of actual_addr, showed B:0
- Info: YES (parser data available)

**Critical Insight:** API calls must use device key (not actual address) for offset devices

### Phase 8: Analyze Data Format
**Goal:** Understand why movement doesn't work

**OLED Display Added:**
```c
// Show raw report bytes
if (it.first >= 128 && (count % 10) == 0) {
    ssd1306_draw_string(&disp, 10, 0, 1, (char*)"MOUSE RAW DATA");
    snprintf(hex, sizeof(hex), "%02X %02X %02X %02X", 
             js[0], js[1], js[2], js[3]);
    ssd1306_draw_string(&disp, 5, 20, 1, hex);
    ssd1306_show(&disp);
}
```

**Key Findings:**
- Idle: `00 00 FF 00` (strange 0xFF in byte 2)
- Moving: Bytes 1 and 2 change (X and Y movement)
- Left click: `01 00 XX 00`
- Right click: `02 00 XX 00`
- HID parser extracted: `X:0 Y:0 B:0` (completely wrong!)

**Critical Insight:** HID parser failed for this device, need direct byte parsing

## Root Cause Analysis

### Problem 1: Callback Suppression
**Code:**
```c
// Only call app callback for first interface of this device address
if (first_interface) {
    tuh_hid_mounted_cb(dev_addr);
}
```

**Issue:** Prevented mouse callback on instance 1 (keyboard was instance 0)

**Fix:**
```c
// Always call mounted callback for boot protocol mice
tuh_hid_mounted_cb(dev_addr | 0x80);  // Marker bit
```

### Problem 2: Address Collision
**Code:**
```c
device[dev_addr] = buffer;  // Keyboard and mouse both use dev_addr=1
```

**Issue:** Mouse overwrote keyboard in device map (same key)

**Fix:**
```c
// Decode marker bit and use offset addressing
if (is_marked_mouse && device.find(actual_addr) != device.end()) {
    device[actual_addr + 128] = buffer;  // Offset mouse to key 129
}
```

### Problem 3: API Call Mismatches
**Code:**
```c
tuh_hid_is_busy(actual_addr);  // Called with addr=1
```

**Issue:** Found keyboard (first device at addr=1), not mouse

**Fix:**
```c
tuh_hid_is_busy(it.first);  // Use device key (129)
// find_device(129) decodes to addr=1, finds MOUSE specifically
```

### Problem 4: HID Parser Failure
**Code:**
```c
for (item in report_items) {
    if (item->Usage == USAGE_X) x = GET_VALUE(item);
}
```

**Issue:** Parser returned X:0 Y:0 for all reports despite valid raw data

**Fix:**
```c
// Direct boot protocol parsing for multi-interface mice
if (it.first >= 128) {
    int8_t buttons = js[0];
    int8_t dx = (int8_t)js[1];
    int8_t dy = (int8_t)js[2];
    // Filter idle quirk
    if (js[2] == 0xFF && js[1] == 0x00) dy = 0;
}
```

## Debugging Best Practices (No Console)

### 1. Progressive Layering
Start at lowest level (USB enumeration) and work up to application layer:
1. USB detection (VID/PID/protocol)
2. Descriptor parsing
3. Callback invocation
4. Registration/storage
5. Handler processing
6. Data parsing

### 2. Strategic Display Timing
- **Initial detection:** 3-second display (one-time event)
- **Parsing results:** 2-second display (one-time per device)
- **Periodic status:** Every 100-500 iterations (ongoing monitoring)
- **Live data:** Every 10 iterations (for rapid feedback)
- **Never:** Continuous updates (causes blocking/lag)

### 3. Information Density
Pack maximum info in 128x64 pixels:
```
Line 1 (big): Primary status
Line 2 (small): Key parameters (addr, instance, etc)
Line 3 (small): Secondary data (counts, flags)
Line 4 (small): Tertiary data (raw bytes, etc)
```

### 4. Conditional Debug Scoping
```c
#if 0  // Disabled for performance
// Debug code here
#endif

// To debug: change #if 0 to #if 1
// To release: change back to #if 0
```

### 5. Hex Dump for Unknown Formats
When data format is unknown:
```c
snprintf(hex, sizeof(hex), "%02X %02X %02X %02X", 
         data[0], data[1], data[2], data[3]);
```
Essential for reverse-engineering proprietary formats

### 6. Comparison Display
Show both raw and parsed data side-by-side:
```
RAW: 00 01 FF 00
PARSED: X:1 Y:-1 B:00
```
Reveals parsing errors instantly

### 7. State Machine Tracking
For complex flows, show progression:
```
DETECTED → PARSED → CALLBACK → REGISTERED → PROCESSING
```
Identifies exactly where pipeline breaks

## Specific Techniques Used

### Device Enumeration Debug
```c
void tuh_hid_mount_cb(...) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 10, 0, 1, (char*)"HID DEVICE");
    snprintf(line, sizeof(line), "Addr:%d Inst:%d", dev_addr, instance);
    snprintf(line, sizeof(line), "VID:%04X PID:%04X", vid, pid);
    snprintf(line, sizeof(line), "P:%d L:%d", protocol, desc_len);
    ssd1306_show(&disp);
    sleep_ms(3000);
}
```

### Parser Result Verification
```c
if (USB_ProcessHIDReport(...) == HID_PARSE_Successful) {
    const char* type_str = (filter_type == HID_MOUSE) ? "MOUSE" : "UNKNOWN";
    ssd1306_draw_string(&disp, 20, 10, 2, (char*)"PARSED");
    snprintf(line, sizeof(line), "Type: %s", type_str);
    ssd1306_show(&disp);
    sleep_ms(2000);
}
```

### Callback Invocation Tracking
```c
// Before calling application callback
ssd1306_draw_string(&disp, 10, 0, 1, (char*)"CALLING CB");
snprintf(line, sizeof(line), "Type: %s", filter_type == HID_MOUSE ? "MOUSE" : "KB");
uint8_t cb_addr = (filter_type == HID_MOUSE) ? (dev_addr | 0x80) : dev_addr;
snprintf(line, sizeof(line), "Addr: %d", cb_addr);
ssd1306_show(&disp);
```

### Device Map Inspection
```c
// Show application-level device registration
ssd1306_draw_string(&disp, 10, 0, 1, (char*)"DEVICE MAP");
snprintf(line, sizeof(line), "Devices: %d", (int)device.size());
for (auto it : device) {
    HID_TYPE tp = tuh_hid_get_type(it.first);
    const char* type = (tp == HID_MOUSE) ? "M" : "K";
    snprintf(line, sizeof(line), "Addr %d: %s", it.first, type);
}
```

### Report Activity Monitoring
```c
// Show reports are actually flowing
static uint32_t report_count = 0;
if ((report_count++ % 100) == 0) {
    snprintf(line, sizeof(line), "Reports: %lu", report_count);
    snprintf(line, sizeof(line), "Len:%d A:%d I:%d", len, dev_addr, instance);
}
```

### Handler State Verification
```c
// Confirm handler finds device and can access it
ssd1306_draw_string(&disp, 15, 0, 1, (char*)"MOUSE HANDLER");
snprintf(line, sizeof(line), "Key:%d Addr:%d", device_key, actual_addr);
bool mounted = tuh_hid_is_mounted(device_key);
bool busy = tuh_hid_is_busy(device_key);
snprintf(line, sizeof(line), "M:%d B:%d", mounted, busy);
```

### Raw Data Inspection
```c
// When parsed values don't match expectations
snprintf(hex, sizeof(hex), "%02X %02X %02X %02X", js[0], js[1], js[2], js[3]);
ssd1306_draw_string(&disp, 5, 20, 1, hex);
// Compare with parsed values
snprintf(parsed, sizeof(parsed), "X:%d Y:%d B:%d", x, y, buttons);
ssd1306_draw_string(&disp, 5, 40, 1, parsed);
```

## Key Lessons Learned

### 1. Multi-Interface Device Complexity
**Challenge:** Single USB address, multiple HID interfaces (keyboard + mouse)
**Solution:** Offset addressing scheme (mouse key = address + 128)
**Learning:** Device maps need unique keys; USB address alone insufficient

### 2. Parser Limitations
**Challenge:** HID parser returned X:0 Y:0 despite valid raw data
**Solution:** Direct byte parsing for boot protocol format
**Learning:** Don't assume parsers work; verify with raw data comparison

### 3. API Consistency Critical
**Challenge:** Using actual_addr in some API calls, device_key in others
**Solution:** Consistently use device_key throughout handler
**Learning:** Mixed addressing schemes cause subtle bugs (e.g., "always busy")

### 4. Callback Scope Issues
**Challenge:** "First interface only" logic broke multi-interface devices
**Solution:** Always call callback for specific device types (MOUSE)
**Learning:** Optimization assumptions break with edge cases

### 5. Idle State Quirks
**Challenge:** Logitech reports `00 00 FF 00` when idle (Y=0xFF)
**Solution:** Filter pattern: if X=0 and Y=0xFF, treat Y as 0
**Learning:** Device-specific quirks require special handling

## Debug Code Template

### Minimal USB Device Debug
```c
// Shows enumeration for every device - use to confirm detection
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, ...) {
    extern ssd1306_t disp;
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 1, (char*)"USB DEVICE");
    char line[20];
    snprintf(line, 20, "A:%d I:%d", dev_addr, instance);
    ssd1306_draw_string(&disp, 0, 15, 1, line);
    snprintf(line, 20, "VID:%04X PID:%04X", vid, pid);
    ssd1306_draw_string(&disp, 0, 30, 1, line);
    ssd1306_show(&disp);
    sleep_ms(2000);
}
```

### Live Data Monitor
```c
// Updates periodically - use for ongoing monitoring
static uint32_t count = 0;
if ((count++ % 100) == 0) {
    // Update OLED
    // NO sleep_ms() - let it update naturally
}
```

### Hex Dump Utility
```c
// Essential for unknown data formats
void show_hex_dump(const uint8_t* data, uint16_t len) {
    char hex[20];
    for (int line = 0; line < 4 && line*4 < len; line++) {
        snprintf(hex, 20, "%02X %02X %02X %02X",
                 data[line*4+0], data[line*4+1], 
                 data[line*4+2], data[line*4+3]);
        ssd1306_draw_string(&disp, 0, 15 + line*15, 1, hex);
    }
}
```

## Tools and Techniques

### OLED Display Capabilities
- **Resolution:** 128x64 pixels
- **Text modes:** 1x (small), 2x (medium/large)
- **Max lines:** ~4 lines of size-1 text, ~2 lines of size-2
- **Update time:** ~50ms per full refresh
- **Blocking:** sleep_ms() necessary for human readability

### Information Prioritization
When screen space limited, show:
1. **State/Status** (working/broken/waiting)
2. **Key identifiers** (address, VID, type)
3. **Critical values** (counts, flags, hex bytes)
4. **Optional details** (only if space available)

### Performance Considerations
- Avoid OLED updates in tight loops (causes lag)
- Use modulo to update every Nth iteration
- Disable all debug for production (`#if 0`)
- Keep debug code in source for future troubleshooting

## Success Metrics

### Before Debugging
- Mouse: Not detected ❌
- Device map: 1 device (keyboard only)
- Reports: None visible

### After Debugging  
- Mouse: Fully working ✅
- Device map: 2 devices (keyboard + mouse)
- Reports: 180+ per second
- Movement: Smooth and responsive
- Buttons: Left and right working correctly

## Reusability

All debug displays wrapped in `#if 0` blocks for future use:
- Change to `#if 1` to re-enable specific debug
- No code deletion - preserved for troubleshooting
- Each debug has clear comment explaining purpose
- Can mix-and-match as needed for different issues

## Time Investment
- Total debugging session: ~2 hours
- Issues diagnosed: 4 major problems
- Debug displays created: 8 different stages
- Iterations to success: ~15 build/test cycles

**Result:** Complex multi-interface USB device fully supported with no serial console access!


