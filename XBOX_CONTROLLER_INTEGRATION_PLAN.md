# Xbox Controller Integration Plan

## Overview

Adding Xbox One controller support alongside existing HID joystick support to the Atari ST USB adapter.

**Reference Implementation:** [Vision Board TinyUSB XPad Project](https://github.com/RT-Thread-Studio/sdk-bsp-ra8d1-vision-board/tree/master/projects/usb/vision_board_tinyusb_xpad)

---

## Current Architecture

### Existing Joystick Support

**File:** `src/hid_app_host.c` + `src/HidInput.cpp`

**Current Implementation:**
- TinyUSB Host stack handles USB HID devices
- Generic HID joystick support via HID parser
- Reports mapped to Atari ST joystick format
- Button/axis data sent to HD6301 emulator

**Data Flow:**
```
USB Joystick → TinyUSB Host → HID Parser → HidInput::handle_joystick() 
                                                 ↓
                                         Atari ST format → 6301 (DR2/DR4)
```

---

## Xbox One Controller Differences

### XInput Protocol vs HID

**Standard HID Joystick:**
- Uses standard USB HID class
- Vendor-agnostic
- Self-describing via HID descriptors
- Works with generic HID parser

**Xbox One Controller (XInput):**
- Uses vendor-specific protocol (XInput)
- Microsoft-specific
- Requires custom initialization sequence
- Different report format
- More complex input structure

### Key Differences:

| Feature | HID Joystick | Xbox Controller |
|---------|--------------|-----------------|
| **Protocol** | Standard HID | XInput (vendor-specific) |
| **Class** | 0x03 (HID) | 0xFF (Vendor) |
| **Subclass** | Any | 0x5D (Xbox) |
| **Initialization** | None | Required (init packet) |
| **Report Format** | Via descriptors | Fixed format |
| **Rumble** | Not supported | Supported |
| **Triggers** | As axes | Separate analog |
| **D-Pad** | As buttons | As HAT switch |

---

## What Needs to Change

### 1. TinyUSB Configuration

**File:** `include/tusb_config.h`

**Current:**
```c
#define CFG_TUH_HID  4  // Max 4 HID devices
```

**Add:**
```c
#define CFG_TUH_VENDOR  2  // Add vendor-specific device support for Xbox
```

**Why:** Xbox controllers use vendor-specific class (0xFF), not HID class

---

### 2. USB Device Detection

**File:** `src/hid_app_host.c`

**Current Detection (HID only):**
```c
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, ...) {
    // Only handles HID class devices
}
```

**Add Xbox Detection:**
```c
// New callback for vendor-specific devices
bool tuh_vendor_mount_cb(uint8_t dev_addr, tuh_xfer_cb_t complete_cb, uintptr_t user_data) {
    // Check if vendor ID = 0x045E (Microsoft)
    // Check if product ID matches Xbox controller
    // Initialize XInput protocol
}
```

**Xbox Controller IDs:**
- Vendor ID: `0x045E` (Microsoft)
- Product IDs:
  - Xbox One: `0x02DD`, `0x02E3`, `0x02EA`, `0x02FD`
  - Xbox One S: `0x02E0`, `0x02FD`, `0x0B00`, `0x0B05`
  - Xbox Elite: `0x02E3`, `0x0B00`

---

### 3. Xbox Initialization Sequence

**New File:** `src/xinput.c` + `include/xinput.h`

**Required Init Packet:**
```c
// Xbox One requires initialization handshake
const uint8_t xbox_init[] = {
    0x05, 0x20, 0x00, 0x01, 0x00  // Wake up packet
};

void xinput_init(uint8_t dev_addr) {
    // Send init packet to endpoint 0x01
    tuh_vendor_xfer(dev_addr, 0x01, xbox_init, sizeof(xbox_init), ...);
}
```

---

### 4. Xbox Report Format

**Xbox One Input Report Structure:**
```c
typedef struct {
    uint8_t report_id;        // Always 0x20
    uint8_t size;             // Report size
    
    // Buttons (2 bytes)
    uint16_t buttons;         // Bit field of buttons
    
    // Triggers (2 bytes)
    uint8_t trigger_left;     // 0-255
    uint8_t trigger_right;    // 0-255
    
    // Left stick (4 bytes)
    int16_t stick_left_x;     // -32768 to 32767
    int16_t stick_left_y;
    
    // Right stick (4 bytes)
    int16_t stick_right_x;
    int16_t stick_right_y;
    
} xbox_input_report_t;
```

**Button Mapping:**
```c
#define XBOX_BTN_A          0x1000
#define XBOX_BTN_B          0x2000
#define XBOX_BTN_X          0x4000
#define XBOX_BTN_Y          0x8000
#define XBOX_BTN_LB         0x0100
#define XBOX_BTN_RB         0x0200
#define XBOX_BTN_BACK       0x0020
#define XBOX_BTN_START      0x0010
#define XBOX_BTN_LS         0x0040  // Left stick click
#define XBOX_BTN_RS         0x0080  // Right stick click
#define XBOX_DPAD_UP        0x0001
#define XBOX_DPAD_DOWN      0x0002
#define XBOX_DPAD_LEFT      0x0004
#define XBOX_DPAD_RIGHT     0x0008
```

---

### 5. Atari ST Mapping

**Current Atari Format:**
```c
// DR4 (directions) - 8 bits
// Bit 0-3: Joystick 0 (Up, Down, Left, Right)
// Bit 4-7: Joystick 1 (Up, Down, Left, Right)

// DR2 (fire buttons) - 3 bits
// Bit 1: Joystick 0 fire
// Bit 2: Joystick 1 fire
```

**Xbox to Atari Mapping Options:**

**Option 1: Simple (Atari ST compatibility)**
```c
// Left stick → Directions
// A button → Fire

xbox_to_atari_joystick(xbox_input_report_t* xbox, uint8_t joy_num) {
    uint8_t directions = 0;
    
    // Left stick to D-Pad (with deadzone)
    if (xbox->stick_left_y < -8000) directions |= JOY_UP;
    if (xbox->stick_left_y > 8000)  directions |= JOY_DOWN;
    if (xbox->stick_left_x < -8000) directions |= JOY_LEFT;
    if (xbox->stick_left_x > 8000)  directions |= JOY_RIGHT;
    
    // A button = Fire
    uint8_t fire = (xbox->buttons & XBOX_BTN_A) ? 1 : 0;
    
    return format_atari_joystick(directions, fire, joy_num);
}
```

**Option 2: Advanced (Multi-button support)**
```c
// Could map multiple Xbox buttons to Atari inputs:
// - Use right stick for second joystick
// - Map triggers to fire buttons
// - Use shoulder buttons for additional functions
```

---

### 6. Integration with Existing Code

**File:** `src/HidInput.cpp`

**Current:**
```cpp
void HidInput::handle_joystick() {
    // Only handles HID joysticks
    for (auto it : hid_joysticks) {
        // Parse HID report
    }
}
```

**Add Xbox Support:**
```cpp
void HidInput::handle_joystick() {
    // Handle HID joysticks
    for (auto it : hid_joysticks) {
        // Parse HID report
    }
    
    // Handle Xbox controllers
    for (auto it : xbox_controllers) {
        xbox_input_report_t* xbox = it.second;
        
        // Map Xbox to Atari format
        uint8_t atari_joy = xbox_to_atari_joystick(xbox, it.first);
        
        // Update joystick state
        update_joystick_state(it.first, atari_joy);
    }
}
```

---

## Implementation Steps

### Phase 1: Basic Xbox Detection (Week 1)

1. **Enable vendor device support in TinyUSB**
   - Modify `tusb_config.h`
   - Add `CFG_TUH_VENDOR` support

2. **Implement device detection**
   - Add `tuh_vendor_mount_cb()` 
   - Check for Microsoft vendor ID (0x045E)
   - Identify Xbox controller models

3. **Test connection**
   - Verify Xbox controller is detected
   - Print device info to console

### Phase 2: XInput Protocol (Week 2)

1. **Create xinput module**
   - `src/xinput.c` + `include/xinput.h`
   - Implement initialization sequence
   - Handle input reports

2. **Parse Xbox reports**
   - Define `xbox_input_report_t` structure
   - Parse button states
   - Parse analog sticks and triggers

3. **Test input parsing**
   - Print button presses to console
   - Verify stick values

### Phase 3: Atari ST Integration (Week 3)

1. **Map Xbox to Atari format**
   - Convert stick to directional buttons
   - Map A button to fire
   - Handle deadzone

2. **Integrate with HidInput**
   - Add Xbox controller list
   - Update `handle_joystick()`
   - Send to 6301 emulator

3. **Test with Atari ST**
   - Verify joystick movement works
   - Test fire button
   - Check game compatibility

### Phase 4: Advanced Features (Week 4)

1. **Multiple controllers**
   - Support 2 Xbox controllers
   - Map to Joystick 0 and 1

2. **Configuration options**
   - Deadzone adjustment
   - Button mapping customization
   - Trigger as fire option

3. **LED feedback**
   - Control Xbox LED ring
   - Indicate player number

---

## Code Structure

### New Files to Create

```
include/
  xinput.h              - Xbox controller definitions and API

src/
  xinput.c              - Xbox protocol implementation
  xbox_mapping.c        - Xbox to Atari mapping functions
```

### Files to Modify

```
include/
  tusb_config.h         - Add vendor device support
  HidInput.h            - Add Xbox controller handling

src/
  hid_app_host.c        - Add vendor mount callback
  HidInput.cpp          - Integrate Xbox controllers
  main.cpp              - Initialize Xbox support
```

---

## Challenges and Solutions

### Challenge 1: Protocol Complexity

**Issue:** XInput is more complex than HID
**Solution:** Start with basic button/stick support, add features incrementally

### Challenge 2: Multiple Device Types

**Issue:** Need to handle both HID joysticks AND Xbox controllers
**Solution:** Separate device lists, unified output format to Atari

### Challenge 3: Initialization Timing

**Issue:** Xbox needs init packet before it sends data
**Solution:** State machine for device initialization

### Challenge 4: Deadzone Calibration

**Issue:** Xbox sticks are very sensitive, may cause drift
**Solution:** Implement configurable deadzone (default ~25%)

---

## Testing Strategy

### Unit Tests

1. **Xbox Detection**
   - Plug in Xbox controller
   - Verify detected and initialized

2. **Button Mapping**
   - Press each Xbox button
   - Verify correct Atari mapping

3. **Stick Calibration**
   - Move sticks in all directions
   - Check deadzone works
   - Verify no drift when centered

### Integration Tests

1. **Mixed Controllers**
   - Connect HID joystick + Xbox controller
   - Both should work simultaneously

2. **Game Compatibility**
   - Test various Atari ST games
   - Verify joystick control works
   - Check for any timing issues

3. **Performance**
   - Monitor USB polling rate
   - Check for input lag
   - Verify 6301 emulator keeps up

---

## Reference Implementation Analysis

Based on the [Vision Board XPad project](https://github.com/RT-Thread-Studio/sdk-bsp-ra8d1-vision-board/tree/master/projects/usb/vision_board_tinyusb_xpad):

**Key Files:**
- `xinput.c` - XInput protocol handler
- `xinput.h` - Protocol definitions
- USB vendor callbacks for Xbox detection

**Key Learnings:**
1. Xbox requires 0x05,0x20 init packet
2. Input reports start with 0x20 ID
3. Reports are 64 bytes total
4. Buttons are in byte 4-5
5. Sticks are 16-bit signed values

**Differences from Vision Board:**
- They use RT-Thread OS, we're bare metal
- They display on screen, we send to Atari ST
- They use different button mapping
- We need to integrate with existing HID code

---

## Estimated Effort

### Development Time

- **Phase 1 (Detection):** 1 week
- **Phase 2 (Protocol):** 1 week  
- **Phase 3 (Integration):** 1 week
- **Phase 4 (Advanced):** 1 week
- **Testing & Debug:** 1 week

**Total:** ~5 weeks for full implementation

### Complexity: MEDIUM-HIGH

**Why:**
- New vendor protocol (not just HID)
- Initialization sequence required
- Integration with existing code
- Multiple device management

---

## Next Steps

1. **Research Phase:**
   - Study Vision Board implementation in detail
   - Review XInput protocol documentation
   - Understand TinyUSB vendor device API

2. **Prototype:**
   - Get Xbox controller detected
   - Parse basic input report
   - Print to console

3. **Integration:**
   - Map to Atari format
   - Test with simple game
   - Refine deadzone/mapping

4. **Polish:**
   - Support multiple controllers
   - Add configuration
   - Comprehensive testing

---

## Success Criteria

✅ Xbox One controller detected and initialized  
✅ Button presses mapped to Atari joystick  
✅ Analog stick controls directions  
✅ Works alongside existing HID joysticks  
✅ No performance degradation  
✅ Games work correctly with Xbox controller  
✅ Documentation and configuration guide  

---

**References:**
- [Vision Board TinyUSB XPad](https://github.com/RT-Thread-Studio/sdk-bsp-ra8d1-vision-board/tree/master/projects/usb/vision_board_tinyusb_xpad)
- [TinyUSB Vendor Device Support](https://docs.tinyusb.org/en/latest/)
- [XInput Protocol Documentation](https://rt-thread.medium.com/vision-board-uses-tinyusb-to-drive-xbox-gamepad-1891f14d20fd)
- [Flight Stick TinyUSB Example](https://github.com/touchgadget/flight_stick_tinyusb)

---

**Version:** 1.0  
**Date:** October 15, 2025  
**Branch:** feature/xbox-controller-integration  
**Status:** 📋 Planning Phase

