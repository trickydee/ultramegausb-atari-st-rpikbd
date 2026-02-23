# Developer Guide

**Purpose:** Onboarding guide for developers and AI assistants working on the Atari ST IKBD emulator project.

**Last Updated:** February 2026

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [System Architecture](#system-architecture)
3. [Code Organization](#code-organization)
4. [Adding New Controllers](#adding-new-controllers)
5. [Key Design Decisions](#key-design-decisions)
6. [Common Patterns](#common-patterns)
7. [Build System](#build-system)
8. [Dependencies](#dependencies)
9. [Debugging](#debugging)
10. [Testing](#testing)

---

## Quick Start

### What This Project Does

Emulates the Atari ST IKBD (Intelligent Keyboard) controller using a Raspberry Pi Pico, allowing modern USB and Bluetooth keyboards, mice, and gamepads to work with vintage Atari ST computers.

### Key Concepts

- **HD6301 Emulator:** Core 1 runs a full emulation of the Motorola HD6301 microcontroller
- **Dual-Core Architecture:** Core 0 handles USB/Bluetooth, Core 1 handles 6301 emulation
- **Serial Communication:** Bidirectional UART communication with the Atari ST (7812 baud)
- **Controller Support:** Multiple USB gamepads, Bluetooth gamepads (Pico 2 W only), keyboards, mice

### Project Structure

```
.
├── src/              # Source code
├── include/          # Header files
├── 6301/            # HD6301 emulator core
├── docs/             # Documentation
├── hardware/         # KiCad PCB designs
├── pico-sdk/         # Git submodule (Raspberry Pi SDK)
└── bluepad32/        # Git submodule (Bluetooth gamepad library)
```

---

## System Architecture

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Raspberry Pi Pico                        │
│                                                              │
│  ┌─────────────────┐              ┌──────────────────┐     │
│  │    Core 0       │              │     Core 1       │     │
│  │  (Main App)     │              │ (HD6301 Emulator)│     │
│  │                 │              │                  │     │
│  │ • USB Host      │◄───Serial───►│ • HD6301 CPU     │     │
│  │ • Bluetooth     │    (UART1)   │ • 6301 ROM       │     │
│  │ • Keyboard      │              │ • Serial I/O     │     │
│  │ • Mouse         │              │ • Tight loop     │     │
│  │ • Gamepads      │              │                  │     │
│  │ • OLED Display  │              │                  │     │
│  │ • UI Buttons    │              │                  │     │
│  └─────────────────┘              └──────────────────┘     │
│         │                                │                  │
└─────────┼────────────────────────────────┼──────────────────┘
          │                                │
          ▼                                ▼
    USB/Bluetooth                    Atari ST (UART1)
    Devices                            (7812 baud)
```

### Data Flow

1. **Input:** USB/Bluetooth device → Core 0 (USB host/Bluepad32) → Input processing → Core 0 → Core 1 (via serial) → HD6301 emulator → Serial output → Atari ST

2. **Commands:** Atari ST → Serial (UART1) → Core 1 → HD6301 emulator → Response → Serial output → Atari ST

### Core Responsibilities

#### Core 0 (Main Application)
- USB host initialization and device management (TinyUSB)
- Bluetooth initialization and device management (Bluepad32)
- Keyboard/mouse/gamepad input processing
- OLED display updates
- UI button handling
- Serial port management (UART1 to Atari ST)
- Runtime mode selection (USB/Bluetooth/USB+BT)

#### Core 1 (HD6301 Emulator)
- HD6301 CPU emulation
- 6301 ROM firmware execution
- Serial I/O handling (RX/TX with Core 0)
- Tight loop execution (no delays, maximum CPU utilization)
- Critical timing constraints (1MHz emulated clock)

### Critical Timing Constraints

- **6301 Clock:** Emulated at 1MHz (CYCLES_PER_LOOP = 1000 cycles per iteration)
- **Serial Baud Rate:** 7812 bits/second (Atari ST standard)
- **Core 1 Loop:** No delays - tight loop for maximum performance
- **Core 0 Polling:** USB/Bluetooth polled every 10ms, serial RX checked every loop iteration

### Communication Between Cores

- **Serial UART1:** Core 0 ↔ Core 1 (commands and responses)
- **Shared Memory:** Global flags for Core 1 pause/resume
- **Flash Coordination:** `flash_safe_execute()` for Bluetooth flash writes (pauses Core 1)

---

## Code Organization

### Directory Structure

```
src/
├── main.cpp                  # Core 0 main loop, initialization
├── HidInput.cpp              # Input processing (keyboard, mouse, joysticks)
├── SerialPort.cpp            # Serial communication with Atari ST
├── UserInterface.cpp         # OLED display and UI buttons
├── AtariSTMouse.cpp          # Mouse input handling
├── hid_app_host.c            # USB HID device management
├── xinput_atari.cpp          # Xbox controller integration
├── xinput_host.c             # XInput driver wrapper
├── ps3_controller.c          # PS3 DualShock 3 support
├── ps4_controller.c          # PS4 DualShock 4 support
├── switch_controller.c       # Nintendo Switch controller support
├── stadia_controller.c       # Google Stadia controller support
├── psc_controller.c          # PlayStation Classic (PSC) support
├── horipad_controller.c      # HORI HORIPAD (Switch) support
├── gamecube_adapter.c        # GameCube USB adapter support
├── bluepad32_init.c          # Bluetooth initialization
├── bluepad32_platform.c      # Bluepad32 platform integration
├── bluepad32_atari.cpp       # Bluetooth gamepad → Atari mapping
└── runtime_toggle.c          # USB/Bluetooth runtime control

include/
├── HidInput.h                # Input processing API
├── config.h                  # Configuration constants (GPIO, timing)
├── version.h                 # Version information
├── xinput.h                  # Xbox controller definitions
├── ps3_controller.h          # PS3 controller API
├── ps4_controller.h          # PS4 controller API
├── switch_controller.h       # Switch controller API
├── stadia_controller.h       # Stadia controller API
├── gamecube_adapter.h        # GameCube adapter API
└── (controller headers)      # One header per controller type
```

### File Naming Conventions

- **C files:** `*.c` - Controller implementations, USB host code
- **C++ files:** `*.cpp` - Main application, input processing, UI
- **Headers:** `*.h` - API definitions, constants
- **Controller files:** `{controller_name}_controller.c` (e.g., `ps4_controller.c`)
- **Mapping functions:** `{controller_name}_to_atari()` in controller file

### Language Usage

- **C:** Controller implementations, USB host code, low-level drivers
- **C++:** Main application, input processing, UI, higher-level abstractions
- **Mixed:** C++ code calls C functions via `extern "C"` declarations

### Key Files Explained

#### `src/main.cpp`
- Entry point for Core 0
- Initializes USB, Bluetooth, OLED, serial port
- Launches Core 1 (HD6301 emulator)
- Main loop: polls USB/Bluetooth, processes input, updates UI

#### `src/HidInput.cpp`
- Central input processing
- Handles keyboard shortcuts
- Integrates all controller types
- Maps controller input to Atari ST format
- Calls controller-specific `get_*_joystick()` functions

#### `src/hid_app_host.c`
- USB HID device management
- Device detection and enumeration
- Report routing to controller modules
- Multi-interface device handling (Logitech Unifying, etc.)

#### Controller Files Pattern
Each controller has:
- `{controller}_controller.c` - Implementation
- `{controller}_controller.h` - API definitions
- `{controller}_is_controller()` - Detection function
- `{controller}_process_report()` - Report parsing
- `{controller}_to_atari()` - Mapping function
- `{controller}_mount_cb()` - Device connected callback
- `{controller}_unmount_cb()` - Device disconnected callback

---

## Adding New Controllers

### Step-by-Step Guide

Follow this pattern to add support for a new USB gamepad controller:

#### Step 1: Create Controller Files

Create header and source files:

```c
// include/my_controller.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MY_CONTROLLER_VENDOR_ID  0x1234
#define MY_CONTROLLER_PRODUCT_ID 0x5678

typedef struct {
    uint8_t dev_addr;
    bool connected;
    // Controller state fields
    uint8_t axis_x;
    uint8_t axis_y;
    bool button_a;
    // ... other fields
} my_controller_t;

bool my_controller_is_controller(uint16_t vid, uint16_t pid);
my_controller_t* my_controller_get_controller(uint8_t dev_addr);
void my_controller_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len);
void my_controller_mount_cb(uint8_t dev_addr);
void my_controller_unmount_cb(uint8_t dev_addr);
void my_controller_to_atari(const my_controller_t* ctrl, uint8_t joystick_num,
                             uint8_t* axis, uint8_t* button);
```

```c
// src/my_controller.c
#include "my_controller.h"
#include "tusb.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>

#define MAX_MY_CONTROLLERS 2
static my_controller_t controllers[MAX_MY_CONTROLLERS];

// Detection function
bool my_controller_is_controller(uint16_t vid, uint16_t pid) {
    return (vid == MY_CONTROLLER_VENDOR_ID && pid == MY_CONTROLLER_PRODUCT_ID);
}

// Controller storage management
static my_controller_t* allocate_controller(uint8_t dev_addr) {
    for (int i = 0; i < MAX_MY_CONTROLLERS; i++) {
        if (!controllers[i].connected) {
            memset(&controllers[i], 0, sizeof(my_controller_t));
            controllers[i].dev_addr = dev_addr;
            controllers[i].connected = true;
            return &controllers[i];
        }
    }
    return NULL;
}

static void free_controller(uint8_t dev_addr) {
    for (int i = 0; i < MAX_MY_CONTROLLERS; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected) {
            memset(&controllers[i], 0, sizeof(my_controller_t));
            return;
        }
    }
}

my_controller_t* my_controller_get_controller(uint8_t dev_addr) {
    for (int i = 0; i < MAX_MY_CONTROLLERS; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected) {
            return &controllers[i];
        }
    }
    return NULL;
}

// Report processing
void my_controller_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len) {
    my_controller_t* ctrl = my_controller_get_controller(dev_addr);
    if (!ctrl) return;
    
    // Parse report bytes
    // Update controller state
    // Example:
    // ctrl->axis_x = report[1];
    // ctrl->axis_y = report[2];
    // ctrl->button_a = (report[3] & 0x01) != 0;
}

// Mount callback (device connected)
void my_controller_mount_cb(uint8_t dev_addr) {
    if (!allocate_controller(dev_addr)) {
        printf("My Controller: Max controllers reached\n");
        return;
    }
    
    // Show splash screen
    #if ENABLE_OLED_DISPLAY
    extern ssd1306_t disp;
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 20, 25, 2, (char*)"MY CTRL");
    ssd1306_show(&disp);
    #endif
    
    // Start receiving reports
    tuh_hid_receive_report(dev_addr, 0);
}

// Unmount callback (device disconnected)
void my_controller_unmount_cb(uint8_t dev_addr) {
    free_controller(dev_addr);
}

// Atari ST mapping
void my_controller_to_atari(const my_controller_t* ctrl, uint8_t joystick_num,
                             uint8_t* axis, uint8_t* button) {
    if (!ctrl || !ctrl->connected) {
        *axis = 0;
        *button = 0;
        return;
    }
    
    // Map controller state to Atari joystick format
    // axis: bitfield (bit 0=up, 1=down, 2=left, 3=right)
    // button: 0=pressed, 1=not pressed (active LOW)
    
    *axis = 0;
    if (ctrl->axis_y < 128 - 20) *axis |= 0x01; // Up
    if (ctrl->axis_y > 128 + 20) *axis |= 0x02; // Down
    if (ctrl->axis_x < 128 - 20) *axis |= 0x04; // Left
    if (ctrl->axis_x > 128 + 20) *axis |= 0x08; // Right
    
    *button = ctrl->button_a ? 0 : 1; // Active LOW
}
```

#### Step 2: Add Detection to `hid_app_host.c`

In `tuh_hid_mount_cb()`:

```c
#include "my_controller.h"

// ... existing code ...

// Check for My Controller BEFORE generic HID parsing
bool is_my_controller = my_controller_is_controller(vid, pid);

if (is_my_controller) {
    printf("My Controller detected: VID=0x%04X, PID=0x%04X\n", vid, pid);
    
    hidh_device_t* dev = alloc_device(dev_addr, instance);
    if (!dev) return;
    
    dev->hid_type = HID_JOYSTICK;
    dev->report_size = 64;  // Adjust based on your controller
    
    // Notify controller module
    my_controller_mount_cb(dev_addr);
    
    // Start receiving reports
    tuh_hid_receive_report(dev_addr, instance);
    
    tuh_hid_mounted_cb(dev_addr);
    return;
}

// ... rest of code ...
```

#### Step 3: Add Report Processing to `hid_app_host.c`

In `tuh_hid_report_received_cb()`:

```c
// Check for My Controller
if (my_controller_get_controller(dev_addr)) {
    my_controller_process_report(dev_addr, report, len);
    tuh_hid_receive_report(dev_addr, instance);  // Queue next report
    return;
}
```

#### Step 4: Add Unmount Callback to `hid_app_host.c`

In `tuh_hid_unmounted_cb()`:

```c
// Check for My Controller
if (my_controller_get_controller(dev_addr)) {
    my_controller_unmount_cb(dev_addr);
}
```

#### Step 5: Add to Input Processing (`HidInput.cpp`)

Add `get_my_controller_joystick()` function:

```cpp
bool HidInput::get_my_controller_joystick(int joystick_num, uint8_t& axis, uint8_t& button) {
    extern my_controller_t* my_controller_get_controller(uint8_t dev_addr);
    extern void my_controller_to_atari(const my_controller_t* ctrl, uint8_t joystick_num,
                                        uint8_t* axis, uint8_t* button);
    
    // Get first connected controller (or implement multi-controller logic)
    // This is simplified - see existing controllers for full implementation
    my_controller_t* ctrl = my_controller_get_first_connected();
    if (!ctrl) return false;
    
    my_controller_to_atari(ctrl, joystick_num, &axis, &button);
    return true;
}
```

Add to joystick priority chain in `handle_joystick()`:

```cpp
// In HidInput::handle_joystick(), add to priority list:
if (get_my_controller_joystick(joystick_num, axis, button)) {
    // Use this controller
    return;
}
```

#### Step 6: Add to Build System (`CMakeLists.txt`)

```cmake
# Add source file
list(APPEND SOURCES
    src/my_controller.c
    # ... other sources
)
```

#### Step 7: Test

1. Build firmware: `./build-all.sh`
2. Flash to Pico
3. Connect controller
4. Verify detection (OLED splash screen)
5. Test input (check joystick movement/fire)
6. Test hot-swap (unplug/plug)

### Common Patterns

#### Controller Storage
- Static array of controller structs (MAX_CONTROLLERS = 2 typically)
- `allocate_controller()` - Find free slot, initialize
- `free_controller()` - Clear slot on disconnect
- `get_controller()` - Find controller by dev_addr

#### Report Processing
- Parse raw USB HID report bytes
- Update controller state struct
- Apply deadzone to analog sticks
- Handle button states (pressed/not pressed)

#### Atari Mapping
- **Axis:** 4-bit field (bit 0=up, 1=down, 2=left, 3=right)
- **Button:** 0=pressed, 1=not pressed (active LOW)
- **Deadzone:** Typically 15-25% for analog sticks
- **Priority:** D-Pad overrides analog stick

#### Splash Screen
- Show controller name on OLED
- Display for ~1-2 seconds
- Wrap in `#if ENABLE_OLED_DISPLAY`

### Reference Implementations

- **Simple HID Controller:** See `stadia_controller.c` (11-byte report, manual parsing)
- **Minimal Report (no sticks):** See `psc_controller.c` (3-byte PSC report, d-pad + buttons only)
- **Compact Report (buttons + 4 axes):** See `horipad_controller.c` (7-byte HORIPAD report)
- **Complex Controller:** See `ps4_controller.c` (multiple PIDs, initialization)
- **PS5-style Report:** See `ps5_controller.c` (report ID 0x01/0x31, DualSense)
- **Non-Standard Protocol:** See `xinput_atari.cpp` and `xinput_host.c` (custom protocol, endpoint management)
- **Bluetooth Controller:** See `bluepad32_atari.cpp` (Bluepad32 integration)

---

## Key Design Decisions

### Why TinyUSB 0.19.0?

- **Current SDK Compatibility:** Works with Pico SDK 2.2.0
- **XInput Support:** Official `xinput_host.h` driver available
- **Limitations:** Vendor class support is incomplete (workaround: use endpoint API)
- **Future:** Consider upgrading for better vendor class support

### Why Bluepad32 for Bluetooth?

- **Gamepad Focus:** Designed specifically for gamepad support
- **Wide Compatibility:** Supports many controller types (Xbox, PlayStation, Switch, etc.)
- **Pico W Support:** Has Pico W platform implementation
- **Alternative:** Could use btstack directly (more complex, less gamepad-focused)

### Why Dual-Core Architecture?

- **Timing Isolation:** HD6301 emulation needs precise timing (1MHz)
- **Performance:** Core 1 runs tight loop, Core 0 handles I/O
- **Independence:** Core 1 unaffected by USB/Bluetooth operations
- **Real Hardware:** Matches original design (separate microcontroller)

### Why CYCLES_PER_LOOP = 1000?

- **Emulated Clock:** HD6301 runs at 1MHz
- **Timing Accuracy:** 1000 cycles ≈ 1ms at 1MHz
- **Historical:** Matches logronoid's reference implementation
- **Critical:** Changing this affects serial timing and compatibility

### Why XIP Mode for Bluetooth Builds?

- **RAM Constraint:** Bluetooth stack requires significant RAM
- **Trade-off:** XIP uses flash (slower) but saves RAM
- **Performance Impact:** Minimal for this application (Core 1 unaffected)
- **Alternative:** Could use RAM builds with RP2350 (more RAM available)

### Why 225MHz Clock for Bluetooth?

- **CYW43 Stability:** CYW43 chip has issues at very high speeds (270MHz causes STALL timeouts)
- **Balance:** 225MHz provides good performance and stability
- **Reference:** Matches logronoid's stable configuration
- **USB Builds:** Use 270MHz (no Bluetooth constraints)

### Why Core 1 Pause During Bluetooth Enumeration?

- **Flash Conflicts:** Bluetooth pairing writes to flash (TLV storage)
- **Core 1 Freeze Risk:** Core 1 accessing flash during BT write can freeze
- **Solution:** Pause Core 1 during enumeration, resume after
- **Timing:** 10ms delay after device ready ensures BTStack operations complete

### Why Serial RX Checked Every Loop Iteration?

- **Baud Rate:** 7812 baud = ~1.28ms per byte
- **Old Approach:** Checked every 10ms (could miss 7-8 bytes)
- **New Approach:** Check every loop (~10-20μs) catches all bytes
- **Critical:** Prevents byte loss during multi-byte IKBD commands

---

## Common Patterns

### Controller Detection Pattern

```c
// 1. VID/PID check in mount callback
bool is_my_controller = my_controller_is_controller(vid, pid);
if (is_my_controller) {
    // Allocate device, set HID type, call mount callback
    // Start receiving reports
    return;
}
```

### Report Processing Pattern

```c
void my_controller_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len) {
    my_controller_t* ctrl = my_controller_get_controller(dev_addr);
    if (!ctrl) return;
    
    // Parse report bytes
    // Update controller state
    // Apply deadzone, handle edge cases
}
```

### Atari Mapping Pattern

```c
void my_controller_to_atari(const my_controller_t* ctrl, uint8_t joystick_num,
                             uint8_t* axis, uint8_t* button) {
    *axis = 0;
    // Map directions (apply deadzone)
    if (y < center - deadzone) *axis |= 0x01; // Up
    if (y > center + deadzone) *axis |= 0x02; // Down
    if (x < center - deadzone) *axis |= 0x04; // Left
    if (x > center + deadzone) *axis |= 0x08; // Right
    
    // Map buttons (active LOW)
    *button = (button_pressed) ? 0 : 1;
}
```

### Splash Screen Pattern

```c
#if ENABLE_OLED_DISPLAY
extern ssd1306_t disp;
ssd1306_clear(&disp);
ssd1306_draw_string(&disp, x, y, size, (char*)"CONTROLLER");
ssd1306_show(&disp);
sleep_ms(1000);  // Show for 1 second
#endif
```

### Error Handling Pattern

```c
// Check for errors, log but don't crash
if (len < expected_len) {
    printf("My Controller: Report too short (%d bytes)\n", len);
    return false;
}

// Return bool for success/failure
// Let calling code handle errors gracefully
```

---

## Build System

### CMake Structure

- **Board Types:** `pico`, `pico2`, `pico_w`, `pico2_w`
- **Options:** `ENABLE_OLED_DISPLAY`, `ENABLE_SERIAL_LOGGING`, `ENABLE_BLUEPAD32`
- **Language Selection:** `LANGUAGE` (EN, FR, DE, SP, IT)

### Build Scripts

- **`build-all.sh`:** Builds all board variants (standard + speed)
- **`build-wireless.sh`:** Builds Bluetooth variants
- **Output:** `.uf2` files in `dist/` directory

### Build Variants

- **Standard:** Full features, OLED enabled, logging enabled
- **Speed:** No OLED, minimal logging, optimized for performance
- **Debug:** Additional debug displays and verbose logging

### Adding New Features

1. Add source files to `CMakeLists.txt`
2. Add configuration options if needed
3. Use conditional compilation (`#if ENABLE_FEATURE`)
4. Update build scripts if needed

---

## Dependencies

### Git Submodules

- **`pico-sdk`:** Raspberry Pi Pico SDK (branch 2.2.0)
- **`bluepad32`:** Bluetooth gamepad library (for Pico 2 W builds)

### External Libraries

- **TinyUSB:** Embedded in Pico SDK (version 0.19.0)
- **BTStack:** Embedded in Bluepad32 (for Bluetooth)
- **SSD1306:** OLED display driver (included in project)

### Version Requirements

- **Pico SDK:** 2.2.0 (pinned in `.gitmodules`)
- **TinyUSB:** 0.19.0 (embedded in SDK)
- **CMake:** 3.13 or later

### Why These Versions?

- **Pico SDK 2.2.0:** Stable, well-tested, good Bluetooth support
- **TinyUSB 0.19.0:** Latest stable with XInput support
- **Bluepad32:** Latest version with Pico W support

---

## Debugging

### Debugging Approaches

1. **Serial Console:** UART0 at 115200 baud (GP0/GP1)
2. **OLED Display:** Visual feedback for device detection, status
3. **Debug Counters:** USB mount/unmount/report counters
4. **LED Indicators:** GPIO LEDs for status (if available)

### Common Debugging Tasks

#### Controller Not Detected
- Check VID/PID (use `printf` in mount callback)
- Verify detection function is called
- Check USB connection (power, hub)

#### Controller Detected But No Input
- Verify report processing is called
- Check report format (dump raw bytes)
- Verify mapping function is called
- Check joystick priority chain

#### Serial Communication Issues
- Check baud rate (7812 for Atari ST)
- Verify UART pins (GP4/GP5)
- Check for overrun errors (logged in console)
- Verify Core 1 is running (heartbeat counter)

#### Core 1 Freeze
- Check flash coordination (`flash_safe_execute_core_init()`)
- Verify Core 1 pause/resume during Bluetooth enumeration
- Check for blocking operations in Core 1
- Verify tight loop (no delays)

### Debug Configuration

Enable debug features:

```cmake
# In CMakeLists.txt or build command
-DENABLE_DEBUG=1
-DENABLE_SERIAL_LOGGING=1
```

Or use build script:

```bash
DEBUG=1 ./build-all.sh
```

### Reference Documentation

- **`docs/USB_DEBUGGING_METHODOLOGY.md`:** Detailed USB debugging guide
- **`docs/TECHNICAL_NOTES.md`:** Performance optimizations and bug fixes
- **`docs/IMPLEMENTATION_HISTORY.md`:** Controller implementation history

---

## Testing

### Testing Checklist

When adding a new controller:

- [ ] Controller detected (OLED splash screen)
- [ ] Report processing works (verify state updates)
- [ ] Atari mapping correct (directions and fire)
- [ ] Hot-swap works (unplug/plug)
- [ ] Multiple controllers work (if supported)
- [ ] No interference with other controllers
- [ ] No crashes or freezes
- [ ] Works with keyboard shortcuts
- [ ] Works in Llamatron mode (if dual-stick)

### Test Scenarios

1. **Single Controller:** Connect one controller, test all buttons/directions
2. **Multiple Controllers:** Connect 2 controllers, verify both work
3. **Hot-Swap:** Unplug/plug controller, verify reconnection
4. **Mixed Controllers:** Connect different controller types simultaneously
5. **Edge Cases:** Rapid button presses, extreme stick positions, simultaneous inputs

### Hardware Testing

- Test on actual Atari ST hardware (not just emulator)
- Test with different Atari ST models (ST, STe, Mega ST, TT)
- Test with different USB hubs (powered/unpowered)
- Test with different controller firmware versions

---

## Additional Resources

### Documentation Files

- **`README.md`:** User-facing documentation
- **`RELEASE_NOTES.md`:** Version history and changes
- **`docs/IMPLEMENTATION_HISTORY.md`:** Controller implementation summaries
- **`docs/TECHNICAL_NOTES.md`:** Technical details, optimizations, bug fixes
- **`docs/USB_DEBUGGING_METHODOLOGY.md`:** USB debugging guide
- **`docs/custom-mappings.md`:** Controller button mappings

### External Resources

- **Pico SDK Documentation:** https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf
- **TinyUSB Documentation:** https://docs.tinyusb.org/
- **Bluepad32 Documentation:** https://github.com/ricardoquesada/bluepad32
- **Atari ST IKBD Specification:** Hardware documentation (see project references)

### Code Examples

- **Simple Controller:** `stadia_controller.c` - Basic HID controller
- **Minimal (no sticks):** `psc_controller.c` - PlayStation Classic, 3-byte report
- **Compact (4 axes):** `horipad_controller.c` - HORI HORIPAD, 7-byte report
- **Complex Controller:** `ps4_controller.c` - Multiple PIDs, initialization
- **PS5 DualSense:** `ps5_controller.c` - Report ID 0x01/0x31, Llamatron
- **Custom Protocol:** `xinput_atari.cpp` / `xinput_host.c` - Non-standard USB protocol
- **Bluetooth Controller:** `bluepad32_atari.cpp` - Bluetooth integration

---

**Last Updated:** February 2026  
**Version:** 21.0.4

