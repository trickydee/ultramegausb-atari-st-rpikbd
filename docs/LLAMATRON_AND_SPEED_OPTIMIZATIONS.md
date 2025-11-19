# Llamatron Mode and Speed Optimizations

**Version:** 12.5.9  
**Branch:** `feature/6391-overspeed-and-Llamatron-mode`  
**Date:** 2025

## Overview

This document describes the Llamatron dual-stick mode feature and the speed optimizations implemented to improve HD6301 emulator performance and compatibility.

---

## Llamatron Dual-Stick Mode

### Purpose

Llamatron mode allows a single dual-stick USB gamepad to control both Atari ST joystick ports simultaneously. This is essential for twin-stick shooters like *Llamatron* (Llamasoft) and *Robotron 2084* (Williams) that require two physical joysticks but only have one USB gamepad connected.

### Activation

- **Keyboard Shortcut:** `Ctrl+F4`
- **Requirements:**
  - Exactly one USB gamepad must be connected
  - Both Atari ST joystick ports (Joy0 and Joy1) must be set to USB mode
  - The gamepad must have two analog thumbsticks

### Controller Mapping

When Llamatron mode is active:

- **Left Stick + Face Buttons** → **Joystick 1 (Joy1)**
  - Left stick controls Joy1 movement
  - Primary fire button (X/Square on PlayStation, A on Xbox/Switch) controls Joy1 fire

- **Right Stick + Eastern Face Button** → **Joystick 0 (Joy0)**
  - Right stick controls Joy0 movement
  - Eastern face button (Circle on PlayStation, B on Xbox, A on Switch, B on Stadia) controls Joy0 fire

### Supported Controllers

- PlayStation 3 DualShock 3
- PlayStation 4 DualShock 4
- Xbox One / Xbox Series X|S (via XInput)
- Nintendo Switch Pro Controller
- Google Stadia Controller

### Behavior

- **Automatic Suspension:** Mode automatically suspends if:
  - A second USB gamepad is connected
  - Either ST joystick port is switched back to D-SUB (GPIO)
  - Llamatron mode is disabled via `Ctrl+F4`

- **Mouse Lock:** When Llamatron mode is enabled, the ST mouse is automatically disabled to prevent interference with Joy0. The previous mouse state is restored when Llamatron mode is disabled.

- **Visual Feedback:** OLED display shows "LLAMATRON MODE ACTIVE" or "LLAMATRON MODE DISABLED" splash screen when toggled.

### Pause/Unpause Button Support

**Added in v12.5.9**

When Llamatron mode is active, the following buttons can be used to pause/unpause the game:

- **PS4:** Options button
- **PS3:** Start button
- **Xbox:** Menu (BACK) button or Start button
- **Switch:** Plus button
- **Stadia:** Menu button

**Behavior:**
- First press sends 'P' (pause) to the Atari ST
- Second press sends 'O' (unpause) to the Atari ST
- Toggles between pause and unpause states
- Button state is tracked with edge detection (press, not hold)

---

## Speed Optimizations

### HD6301 Emulator Overclocking

The HD6301 emulator can be run at a multiple of its emulated clock speed to improve throughput and reduce serial communication bottlenecks.

**Configuration:**
- **Default:** `HD6301_OVERCLOCK_NUM = 1` (normal speed)
- **Location:** `include/config.h`
- **Usage:** Can be increased to run the emulator faster (e.g., 2x, 3x)

**Implementation:**
- Multiplier applied in `hd6301_run_clocks()` function
- Allows the 6301 emulator to process instructions faster
- Helps prevent serial RX queue overflow during high-load scenarios

### OLED Display Toggle

The OLED display can be disabled at build time to save CPU cycles and reduce binary size.

**Build Options:**
- **Standard Build:** `ENABLE_OLED_DISPLAY=1` (default)
- **Speed Mode Build:** `ENABLE_OLED_DISPLAY=0`

**Benefits:**
- Reduces CPU overhead from display updates
- Smaller binary size (~11KB savings)
- Improved performance for timing-sensitive applications

**Build Variants:**
- Standard: `atari_ikbd_pico.uf2` / `atari_ikbd_pico2.uf2` (with OLED)
- Speed: `atari_ikbd_pico_speed.uf2` / `atari_ikbd_pico2_speed.uf2` (no OLED)

### Serial Logging Toggle

Verbose serial logging can be disabled at build time to reduce console bandwidth and CPU overhead.

**Build Options:**
- **Standard Build:** `ENABLE_SERIAL_LOGGING=1` (default)
- **Speed Mode Build:** `ENABLE_SERIAL_LOGGING=0`

**Benefits:**
- Reduces CPU overhead from `printf()` calls
- Reduces serial console bandwidth
- Improves real-time performance

**Implementation:**
- Most debug/instrumentation logging wrapped in `#if ENABLE_SERIAL_LOGGING`
- Critical error messages still displayed regardless of setting

### Build System

The `build-all.sh` script automatically creates both standard and speed mode builds:

```bash
./build-all.sh
```

**Output:**
- Standard builds: Full features, OLED enabled, logging enabled
- Speed builds: No OLED, minimal logging, optimized for performance

**File Sizes (approximate):**
- Standard RP2040: ~363KB
- Speed RP2040: ~341KB (22KB smaller)
- Standard RP2350: ~344KB
- Speed RP2350: ~321KB (23KB smaller)

---

## Technical Details

### Llamatron Mode Implementation

**Key Files:**
- `src/HidInput.cpp` - Main implementation
- `src/ps4_controller.c` - PS4 dual-stick support
- `src/ps3_controller.c` - PS3 dual-stick support
- `src/switch_controller.c` - Switch dual-stick support
- `src/stadia_controller.c` - Stadia dual-stick support
- `src/xinput_atari.cpp` - Xbox dual-stick support

**State Management:**
- `g_llamatron_mode` - Mode enabled/disabled flag
- `g_llamatron_active` - Mode currently active (requirements met)
- `g_llama_axis_joy1`, `g_llama_fire_joy1` - Joy1 state
- `g_llama_axis_joy0`, `g_llama_fire_joy0` - Joy0 state
- `g_llamatron_restore_mouse` - Mouse state before mode activation

**Pause Button Implementation:**
- `check_llamatron_pause_button()` - Checks all controller types for menu/options/start button
- `g_llama_paused` - Tracks current pause state
- `g_llama_pause_button_prev` - Previous button state for edge detection
- Key injection via `key_states[ATARI_KEY_P]` and `key_states[ATARI_KEY_O]`

### Speed Optimization Implementation

**HD6301 Overclocking:**
- `6301/6301.c` - `hd6301_run_clocks()` applies multiplier
- `include/config.h` - `HD6301_OVERCLOCK_NUM` definition

**Build-Time Toggles:**
- `CMakeLists.txt` - CMake options for `ENABLE_OLED_DISPLAY` and `ENABLE_SERIAL_LOGGING`
- Conditional compilation throughout codebase
- `build-all.sh` - Automated build script for multiple variants

---

## Usage Examples

### Enabling Llamatron Mode

1. Connect a single dual-stick USB gamepad (e.g., Xbox controller)
2. Ensure both Joy0 and Joy1 are set to USB mode (use `Ctrl+F9` and `Ctrl+F10` if needed)
3. Press `Ctrl+F4` to enable Llamatron mode
4. OLED display shows "LLAMATRON MODE ACTIVE"
5. Left stick controls Joy1, right stick controls Joy0

### Pausing/Unpausing in Llamatron Mode

1. With Llamatron mode active, press the menu/options/start button
2. First press sends 'P' to pause the game
3. Second press sends 'O' to unpause the game
4. Continues toggling on each button press

### Building Speed Mode Variants

```bash
# Build all variants (standard + speed)
./build-all.sh

# Or build speed mode only
OLED=0 LOGGING=0 ./build-all.sh
```

---

## Performance Impact

### Speed Mode Benefits

- **CPU Cycles:** ~11KB saved by disabling OLED updates
- **Serial Bandwidth:** Reduced by disabling verbose logging
- **Binary Size:** ~22-23KB smaller for speed builds
- **Compatibility:** Improved timing for sensitive demos (e.g., Dragonnels)

### HD6301 Overclocking Benefits

- **Throughput:** Increased instruction processing rate
- **Serial Processing:** Faster RX queue draining
- **Timing:** Better handling of high-load scenarios

---

## Known Limitations

1. **Llamatron Mode:**
   - Requires exactly one USB gamepad (mode suspends if second pad connected)
   - Both joystick ports must be set to USB
   - Mouse is automatically disabled while mode is active

2. **Speed Mode:**
   - No OLED display feedback (must use serial console for debugging)
   - Reduced logging makes troubleshooting more difficult
   - HD6301 overclocking may affect timing accuracy if set too high

---

## Future Enhancements

Potential improvements for future versions:

- Configurable HD6301 overclock multiplier via keyboard shortcut
- Per-controller pause button mapping customization
- Llamatron mode support for more controller types
- Additional speed optimizations (e.g., reduced polling frequency)

---

## References

- **Branch:** `feature/6391-overspeed-and-Llamatron-mode`
- **Version:** 12.5.9
- **Related Documentation:**
  - `docs/custom-mappings.md` - Controller button mappings
  - `README.md` - General project documentation
  - `docs/SERIAL_PERFORMANCE_OPTIMIZATIONS.md` - Serial communication optimizations

