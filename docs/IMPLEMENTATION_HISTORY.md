# Implementation History

This document consolidates the implementation history of major features and controller support in the Atari ST IKBD emulator.

**Last Updated:** February 2026

**Design rationale:** The notes below include brief "why" context where it helps. Implementations often reflect trade-offs (e.g. compatibility vs complexity, RAM vs speed). For full architecture and patterns, see [docs/DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) and [docs/TECHNICAL_NOTES.md](TECHNICAL_NOTES.md).

---

## Controller Implementations

### Xbox Controller Support (v7.0.0)

**Status:** ✅ Complete

**Implementation:** Full Xbox controller support was added using TinyUSB 0.19.0's low-level USB endpoint API, bypassing the need for vendor class support.

**Key Features:**
- Supports Xbox 360, Xbox One, Xbox Series X|S controllers
- Uses low-level endpoint functions (`tuh_edpt_open()`, `tuh_edpt_xfer()`)
- Custom XInput protocol implementation
- D-Pad and analog stick support with deadzone
- A button and right trigger (>50%) for fire

**Technical Details:**
- Opens interrupt endpoints (IN: 0x81, OUT: 0x01)
- Sends XInput wake-up packet: `{0x05, 0x20, 0x00, 0x01, 0x00}`
- Parses 64-byte Xbox input reports
- Integrates with joystick priority system

**Why this approach:** TinyUSB's vendor class support was insufficient for XInput, so the implementation uses the raw endpoint API (`tuh_edpt_open()`, `tuh_edpt_xfer()`) to send the wake-up packet and poll the interrupt endpoint. This avoids depending on upstream XInput host support and keeps full control over the protocol.

**Files:**
- `include/xinput.h` - XInput protocol definitions
- `src/xinput_atari.cpp` - Xbox-to-Atari mapping
- `src/xinput_host.c` - XInput driver wrapper and endpoint handling

---

### PlayStation 4 DualShock 4 Support

**Status:** ✅ Complete

**Key Features:**
- Full D-Pad and analog stick support
- Face buttons (Triangle, Circle, Cross, Square)
- Shoulder buttons (L1, R1, L2, R2)
- Professional splash screen on OLED
- Hot-swap support

**Implementation:** Standard HID joystick detection with custom report parsing.

---

### PlayStation 3 DualShock 3 Support (v10.0.0)

**Status:** ✅ Complete

**Key Features:**
- VID/PID detection (0x054C:0x0268)
- Proper USB initialization via Feature Report (0xF4): `{0x42, 0x0C, 0x00, 0x00}`
- Stops controller searching for PlayStation console
- Activates LEDs and enables HID reports
- Full input mapping: D-Pad, analog sticks, face buttons, shoulder buttons
- 48-byte report structure with Report ID 0x01

**Files:**
- `include/ps3_controller.h` - PS3 API definitions
- `src/ps3_controller.c` - Complete implementation

---

### Nintendo Switch Pro Controller Support

**Status:** ✅ Complete

**Key Features:**
- Full Switch Pro Controller support
- D-Pad and analog stick mapping
- Face buttons (A, B, X, Y)
- Shoulder buttons (L, R, ZL, ZR)
- Hot-swap support

---

### PowerA Fusion Wireless Arcade Stick (v8.0.0)

**Status:** ✅ Complete

**Problem:** PowerA Fusion Wireless Arcade Stick (PID 0xA715) was being incorrectly detected as a mouse.

**Root Cause:** PID 0xA715 wasn't in the Switch controller detection list, causing the device to go through generic HID parser, which classified it as a mouse due to X/Y axes.

**Solution:**
- Added PID 0xA715 to Switch controller detection
- Updated mount callbacks to display "PowerA Arcade" on OLED
- Enhanced OLED debugging for device identification

**Key Files:**
- `include/switch_controller.h` - Added `POWERA_FUSION_ARCADE_V2` definition
- `src/switch_controller.c` - Updated detection logic

---

### Google Stadia Controller (v9.0.0)

**Status:** ✅ Complete

**Key Features:**
- Full support for Google Stadia Controller (VID 0x18D1, PID 0x9400)
- D-Pad 8-direction control
- Left analog stick with smooth movement
- All face buttons (A, B, X, Y) as fire
- Shoulder buttons (L1, R1) and triggers (L2, R2) as fire
- Professional splash screen

**Technical Implementation:**
- 11-byte report format
- Manual byte parsing (HID descriptor incomplete)
- D-Pad in byte 1 (hat switch format)
- Face/shoulder buttons in byte 3 (bitmask)
- Triggers in bytes 8-9 (analog)

**Why manual parsing:** The Stadia controller's HID descriptor is incomplete or non-standard, so generic HID parsing does not reliably expose axes and buttons. Using the report layout documented by the stadia-vigem project and parsing bytes directly in our driver avoids dependency on a correct descriptor and gives predictable mapping.

**Reference:** Based on stadia-vigem project format

**Files:**
- `include/stadia_controller.h` - Stadia controller definitions
- `src/stadia_controller.c` - Controller management
- `src/HidInput.cpp` - Report parsing and Atari mapping

---

### GameCube USB Adapter Support (v11.0.0+)

**Status:** ✅ Complete

**Implementation:** Support for Nintendo GameCube controllers via USB adapter.

**Key Features:**
- Supports official Nintendo adapter (VID 0x057E, PID 0x0337)
- Third-party adapter support
- Left analog stick control
- D-Pad override
- A button primary fire, B button alternate fire
- Supports up to 4 controllers via adapter

**Technical Details:**
- Uses HID hijacking approach (HID driver claims device, custom logic handles protocol)
- Requires initialization: control transfer + 0x13 write to interrupt pipe
- Report callback processing for controller input

**Why HID hijacking:** The Nintendo adapter exposes a non-standard protocol (control transfer + interrupt write) that does not match a generic HID profile. Letting the HID host claim the device and then running custom init and report handling in the same driver avoids conflicts and keeps one code path for the adapter.

**Files:**
- `src/gamecube_adapter.c` - GameCube adapter implementation
- `src/hid_app_host.c` - HID callbacks with dual init

---

### PlayStation Classic (PSC) and HORI HORIPAD (v21.0.4)

**Status:** ✅ Complete

**Implementation:** USB support for Sony PlayStation Classic controller and HORI HORIPAD for Nintendo Switch.

**PlayStation Classic (PSC):**
- VID 0x054C, PID 0x0CDA; 3-byte HID report (no analog sticks)
- D-pad for direction; Cross and R2 for fire
- OLED splash "PSC / PlayStation Classic"

**HORI HORIPAD (Switch):**
- VID 0x0F0D, PID 0x00C1; 7-byte report (buttons, d-pad, 4 axes)
- D-pad or left stick for direction; B and R2 for fire
- Llamatron dual-stick: left stick + B → Joy 1, right stick + A → Joy 0
- OLED splash "HORI / HORIPAD (Switch)"

**Files:**
- `include/psc_controller.h`, `src/psc_controller.c` - PSC implementation
- `include/horipad_controller.h`, `src/horipad_controller.c` - HORIPAD implementation
- `src/hid_app_host.c` - Mount/report/unmount wiring
- `src/HidInput.cpp` - Joystick priority and Llamatron (HORIPAD)

---

### Bluetooth Support (v21.0.0)

**Status:** ✅ Complete

**Hardware Requirement:** Raspberry Pi Pico 2 W (RP2350) - requires additional RAM not available on original Pico W

**Key Features:**
- Bluetooth keyboards with full shortcut support
- Bluetooth mice with scroll wheel support
- Bluetooth gamepads via Bluepad32 (DualSense, DualShock 4, Switch Pro, Xbox Wireless)
- Mix and match USB, Bluetooth, and 9-pin Atari joysticks
- Runtime mode selection (USB+Bluetooth, USB only, Bluetooth only)
- Bluetooth pairing management

**Technical Implementation:**
- Bluepad32 library integration
- BR/EDR (Bluetooth Classic) protocol for gamepads
- HID protocol for keyboards and mice
- Critical initialization order: CYW43 before I2C/SPI peripherals
- Clock speed: 150 MHz for Bluetooth builds

**Why Bluepad32:** Gamepad support was the main goal; Bluepad32 already handles many controllers (DualSense, DualShock 4, Switch Pro, Xbox Wireless) and has a Pico W platform. Using it avoided reimplementing Bluetooth HID and gamepad protocols. The original Pico W (RP2040) has insufficient RAM for the stack, so Bluetooth is limited to Pico 2 W (RP2350).

**Files:**
- `src/bluepad32_init.c` - Bluepad32 initialization
- `src/bluepad32_platform.c` - Custom platform implementation
- `src/bluepad32_atari.cpp` - Bluepad32 to Atari ST converter

---

## Feature Implementations

### XRESET (Hardware Reset)

**Status:** ✅ Complete

**Implementation:** XRESET functionality allows triggering HD6301 hardware reset via keyboard shortcut (Ctrl+F11).

**Key Features:**
- Keyboard shortcut: `Ctrl+F11`
- Triggers full IKBD reset pulse
- Equivalent to power cycling the IKBD
- OLED display shows "RESET" message

**Technical Details:**
- Uses `hd6301_reset(1)` for cold reset
- Full CPU state reset
- Serial communication reset

---

### Llamatron Dual-Stick Mode

**Status:** ✅ Complete

**Purpose:** Enables twin-stick functionality for modern controllers, allowing a single dual-stick gamepad to control both Atari ST joystick ports simultaneously.

**Activation:**
- Keyboard shortcut: `Ctrl+F4`
- Requirements: Exactly one gamepad (USB or Bluetooth), both joystick ports set to USB mode

**Controller Mapping:**
- **Left stick + face buttons** → **Joystick 1** (movement and fire)
- **Right stick + eastern face button** → **Joystick 0** (movement and fire)

**Supported Controllers:**
- PlayStation 3 DualShock 3
- PlayStation 4 DualShock 4
- PlayStation 5 DualSense / DualSense Edge
- HORI HORIPAD (Switch)
- Xbox One / Xbox Series X|S
- Nintendo Switch Pro Controller
- Google Stadia Controller
- Bluetooth gamepads (via Bluepad32)

**Key Features:**
- Automatic suspension if second gamepad connected
- Mouse automatically disabled while mode active
- Pause/unpause button support (Options/Menu/Start buttons)
- Visual feedback on OLED display

**Why this design:** Twin-stick games (e.g. Llamatron, Robotron) expect two physical joysticks. With one dual-stick gamepad we map left stick + fire to Joy 1 and right stick + one face button to Joy 0 so a single pad can drive both ports. The "exactly one gamepad" rule and automatic suspension keep behaviour predictable and avoid ambiguity when multiple pads are connected.

**Files:**
- `src/HidInput.cpp` - Main implementation

---

## Build and Development Notes

### Version History

- **v21.0.4:** PlayStation Classic (PSC) and HORI HORIPAD (Switch) USB support
- **v21.0.3:** PlayStation 5 DualSense / DualSense Edge USB support
- **v21.0.2:** Expanded PS3/PS4/Switch VID/PIDs (Phase 1)
- **v21.0.1:** Bluetooth support improvements, code cleanup, production build variants
- **v21.0.0:** Major Bluetooth support addition for Pico 2 W
- **v12.5.9:** Llamatron mode and speed optimizations
- **v11.0.0+:** GameCube USB adapter support
- **v10.0.0:** PlayStation 3 DualShock 3 support
- **v9.0.0:** Google Stadia Controller support
- **v8.0.0:** PowerA Fusion Wireless Arcade Stick support
- **v7.0.0:** Xbox controller support (major milestone)

### Development Methodology

**OLED-Only Debugging:**
Many implementations used OLED display for debugging when serial console wasn't available, including:
- VID/PID display on device connection
- Raw byte display for report analysis
- Sticky capture for transient values
- Progressive refinement of byte parsing

**Reference Implementations:**
- stadia-vigem project for Stadia controller format
- Linux kernel drivers for various controllers
- Open-source USB HID parsers

---

## Controller Support Matrix

| Controller Type | Version Added | Status |
|----------------|---------------|--------|
| Generic USB HID Joysticks | Initial | ✅ |
| Xbox 360 / Xbox One / Xbox Series X\|S | v7.0.0 | ✅ |
| PlayStation 4 DualShock 4 | Initial | ✅ |
| PlayStation 5 DualSense / DualSense Edge | v21.0.3 | ✅ |
| PlayStation Classic (PSC) | v21.0.4 | ✅ |
| PlayStation 3 DualShock 3 | v10.0.0 | ✅ |
| Nintendo Switch Pro Controller | Initial | ✅ |
| HORI HORIPAD (Switch) | v21.0.4 | ✅ |
| PowerA Fusion Wireless Arcade Stick | v8.0.0 | ✅ |
| Google Stadia Controller | v9.0.0 | ✅ |
| Nintendo GameCube (USB adapter) | v11.0.0+ | ✅ |
| Bluetooth Gamepads (Pico 2 W) | v21.0.0 | ✅ |
| Bluetooth Keyboards (Pico 2 W) | v21.0.0 | ✅ |
| Bluetooth Mice (Pico 2 W) | v21.0.0 | ✅ |

---

**Note:** This document consolidates historical implementation notes. For current feature documentation, see `README.md` and `RELEASE_NOTES.md`.

