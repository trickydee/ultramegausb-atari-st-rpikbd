# Technical Notes

This document consolidates technical documentation, performance optimizations, bug fixes, and architecture notes for the Atari ST IKBD emulator.

**Last Updated:** January 2, 2026

---

## Performance Optimizations

### Serial Communication Optimizations

**Problem:** The 6301 IKBD emulator was experiencing communication issues with the Atari ST due to timing and buffering problems.

**Issues:**
- Serial RX checked only every 10ms - too slow for 7812 baud communication
- UART FIFO disabled - causing byte drops and data loss
- UI updates blocking main loop - adding unnecessary latency

**Solutions Implemented:**

1. **Move Serial RX Check to Main Loop**
   - Changed from checking every 10ms to checking every loop iteration
   - Serial RX now checked every ~10-20 microseconds instead of 10,000 microseconds
   - Prevents byte loss during high-load periods

2. **Re-enable UART Hardware FIFO**
   - RP2040 UART has 32-byte hardware FIFOs (TX and RX)
   - FIFO prevents data loss and actually reduces lag
   - Enables buffering independently of CPU timing

3. **Optimize RX Byte Reading**
   - Changed from reading one byte per call to reading ALL available bytes
   - Prevents buffer overflow during multi-byte commands
   - More efficient byte processing

**Technical Details:**
- Baud rate: 7812 bits/second
- Time per byte: ~1.28ms (including start/stop bits)
- Original check interval: 10ms (could miss 7-8 bytes)
- New check interval: ~10-20μs (catches all bytes)

**Files Modified:**
- `src/main.cpp` - Moved serial RX check to main loop
- `src/SerialPort.cpp` - Re-enabled UART FIFO, optimized byte reading

---

### Core 1 (HD6301 Emulator) Optimizations

**Changes Implemented:**

1. **Reduced Sleep Granularity (4x Improvement)**
   - Changed `CYCLES_PER_LOOP` from 1000 (1ms) to 250 (250μs)
   - Core 1 wakes 4 times more frequently
   - Serial interrupts processed within 250μs instead of 1ms
   - Multi-byte IKBD commands delivered 4x faster

2. **Accurate TX Status Reporting**
   - Changed from always reporting "TX is empty" to checking actual buffer status
   - Uses `SerialPort::instance().send_buf_empty()` for real status
   - 6301 ROM now knows real TX buffer status
   - Prevents ROM from sending when TX buffer is full
   - Better flow control and more authentic hardware behavior

3. **Serial Overrun Error Monitoring**
   - Added detection for serial overrun errors (ORFE bit)
   - Diagnostic tool for identifying remaining issues
   - Reports errors without spamming (every 100th occurrence)

**Why This Matters:**
- The ROM firmware checks TDRE (Transmit Data Register Empty) flag before sending bytes
- If we always report "empty" when buffer is actually full, bytes might get dropped
- Accurate status reporting improves reliability

**Files Modified:**
- `src/main.cpp` - Core 1 timing and TX status reporting

---

### Speed Optimizations and Build Variants

**HD6301 Emulator Overclocking:**
- Multiplier can be applied in `hd6301_run_clocks()` function
- Allows the 6301 emulator to process instructions faster
- Helps prevent serial RX queue overflow during high-load scenarios
- Configuration: `HD6301_OVERCLOCK_NUM` in `include/config.h`

**OLED Display Toggle:**
- Can be disabled at build time to save CPU cycles
- Standard build: `ENABLE_OLED_DISPLAY=1` (default)
- Speed mode build: `ENABLE_OLED_DISPLAY=0`
- Benefits: ~11KB binary size reduction, reduced CPU overhead

**Serial Logging Toggle:**
- Verbose serial logging can be disabled at build time
- Standard build: `ENABLE_SERIAL_LOGGING=1` (default)
- Speed mode build: `ENABLE_SERIAL_LOGGING=0`
- Benefits: Reduced CPU overhead, improved real-time performance

**Build System:**
- `build-all.sh` script creates both standard and speed mode builds
- Standard builds: Full features, OLED enabled, logging enabled
- Speed builds: No OLED, minimal logging, optimized for performance

**File Sizes (approximate):**
- Standard RP2040: ~363KB
- Speed RP2040: ~341KB (22KB smaller)
- Standard RP2350: ~344KB
- Speed RP2350: ~321KB (23KB smaller)

---

## Bug Fixes

### Core 1 Freeze Fix

**Problem:** Core 1 (HD6301 emulator) would freeze when Bluetooth connected on Pico 2 W.

**Root Cause:** Flash operations (used by Bluetooth stack for pairing storage) require Core 1 to be paused. The `flash_safe_execute()` function coordinates this, but Core 1 wasn't properly initialized for flash-safe execution.

**Solution:**
- Added `flash_safe_execute_core_init()` call in Core 1 initialization
- Ensures Core 1 can be safely paused during flash operations
- Prevents freezes when Bluetooth pairing occurs

**Files Modified:**
- `src/main.cpp` - Core 1 initialization sequence

---

### Xbox Controller Reconnection Issue (v7.3.0)

**Problem:** Xbox controllers stopped working after PS4 controller usage:
- Xbox first → Works ✅
- PS4 used → Works ✅
- Xbox after PS4 → Broken ❌

**Root Cause:** PS4 unmount callback was never being called when controller was unplugged. The PS4 controller stayed marked as "connected" with stale data, blocking Xbox from being checked as a fallback input source.

**Solution:**
- Added proper PS4 unmount callback wiring in `hid_app_host.c`
- Now when PS4 is disconnected, it properly cleans up
- Allows Xbox to work correctly as fallback

**Files Modified:**
- `src/hid_app_host.c` - PS4 unmount callback

---

### IKBD Command Bug Fix

**Problem:** Multi-byte IKBD commands were being processed incorrectly, causing communication errors.

**Solution:** Improved serial RX byte processing to handle complete command sequences.

---

## Architecture and Design Notes

### HD6301 Emulator Architecture

**Core Assignment:**
- **Core 0:** Main application (USB, Bluetooth, UI, keyboard/mouse handling)
- **Core 1:** HD6301 emulator (dedicated CPU emulation)

**Core 1 Timing:**
- Runs in tight loop with `CYCLES_PER_LOOP = 1000` (matches original logronoid implementation)
- Processes 6301 instructions in batches
- Handles serial interrupts between batches
- Accurate 1MHz emulation timing

**Serial Communication:**
- Bidirectional UART communication with Atari ST
- RX: ST → 6301 (commands and data)
- TX: 6301 → ST (responses and events)
- Hardware FIFO enabled for reliable buffering

---

### Bluetooth Integration (Pico 2 W)

**Initialization Order (CRITICAL):**
1. Set clock speed (150 MHz for Bluetooth builds)
2. Initialize CYW43/Bluepad32 **FIRST**
3. Then initialize OLED display (I2C) **AFTER**

**Why:** I2C/SPI initialization before CYW43 interferes with wireless pin configuration, causing STALL timeouts.

**Files:**
- `src/bluepad32_init.c` - Initialization sequence
- `src/main.cpp` - Main loop integration

**Build Configuration:**
- Bluetooth builds use XIP (Execute In Place) to save RAM
- Clock speed: 150 MHz (improves CYW43 stability)
- Conditionally compiled for `pico2_w` board type only

---

### USB HID Architecture

**Device Detection:**
- TinyUSB HID host driver handles device enumeration
- Custom detection logic identifies specific controllers (Xbox, PS3, PS4, Switch, Stadia, GameCube)
- Generic HID joysticks handled via fallback parser

**Report Processing:**
- Standard HID report parsing for most devices
- Custom byte parsing for non-standard controllers (Stadia, PS3, GameCube)
- Priority system: GPIO → USB HID → Specific controllers → Generic HID

**Controller Priority:**
1. GPIO joysticks (if configured)
2. USB HID joysticks
3. Xbox controllers (XInput)
4. PlayStation controllers (PS3/PS4)
5. Nintendo controllers (Switch/GameCube)
6. Stadia controller
7. Generic HID fallback

---

## USB Debugging Methodology

**OLED-Only Debugging:**
- Used when serial console isn't available
- Display VID/PID on device connection
- Show raw report bytes for analysis
- Sticky capture for transient values
- Progressive refinement of byte parsing

**Debug Display Techniques:**
- Multi-page display for large data sets
- Hex display for raw bytes
- Decimal display for processed values
- Status messages for state transitions

**Common Debugging Challenges:**
- Button presses may be too brief for display updates
- Solution: Sticky capture with immediate update on button press
- Multi-byte commands require careful parsing
- Solution: Read all available bytes, not just one

---

## Technical Specifications

### Serial Communication

- **Baud Rate:** 7812 bits/second
- **Parity:** None
- **Data Bits:** 8
- **Stop Bits:** 1
- **UART FIFO:** 32 bytes (TX and RX)
- **Flow Control:** Hardware RTS/CTS

### Timing

- **6301 Clock:** 1MHz (emulated)
- **Core 1 Loop:** 1000 cycles per iteration (~1ms)
- **Serial RX Check:** Every loop iteration (~10-20μs)
- **UI Update:** Every 10ms
- **Bluetooth Poll:** Every 10ms

### Memory

- **RP2040:** 264KB RAM
- **RP2350:** 520KB RAM (required for Bluetooth)
- **Flash:** 2MB (code + pairing storage)
- **XIP Mode:** Used for Bluetooth builds to save RAM

---

## Build System Notes

### CMake Configuration

**Board Types:**
- `pico` - Original Raspberry Pi Pico (RP2040)
- `pico2` - Raspberry Pi Pico 2 (RP2350)
- `pico_w` - Original Pico W (RP2040) - USB only
- `pico2_w` - Pico 2 W (RP2350) - USB + Bluetooth

**Build Options:**
- `ENABLE_OLED_DISPLAY` - Enable/disable OLED display
- `ENABLE_SERIAL_LOGGING` - Enable/disable verbose logging
- `ENABLE_BLUEPAD32` - Enable Bluetooth support (auto-set for pico2_w)
- `DEBUG` - Enable debug mode (build script option)

### Build Scripts

- `build-all.sh` - Builds all board variants (standard + speed)
- `build-wireless.sh` - Builds Bluetooth variants
- Output: `.uf2` files in `dist/` directory

---

## Known Limitations and Workarounds

### Bluetooth (Pico 2 W)

- **Logitech MX devices:** Require re-pairing after each emulator restart
- **CLM firmware warnings:** May appear but don't block functionality
- **CYW43 STALL timeouts:** May occur but don't prevent pairing

### USB

- **Power requirements:** Some controllers require powered USB hub
- **GameCube adapter:** Uses HID hijacking approach (non-standard protocol)
- **PS3 controller:** Requires USB Feature Report initialization

### Performance

- **Serial RX queue:** Can overflow during high-load scenarios (mitigated by optimizations)
- **Core 1 timing:** Must match original implementation for compatibility
- **Bluetooth builds:** Require XIP mode (may affect performance)

---

## Reference Materials

### External Resources

- **TinyUSB:** https://github.com/hathach/tinyusb
- **Bluepad32:** https://github.com/ricardoquesada/bluepad32
- **Pico SDK:** https://github.com/raspberrypi/pico-sdk
- **Stadia-vigem:** https://github.com/lrwerth/stadia-vigem (Stadia controller format)

### Key Specifications

- **HD6301:** Motorola 8-bit microcontroller datasheet
- **Atari ST IKBD:** Hardware specification
- **USB HID:** USB Human Interface Device specification
- **XInput:** Microsoft XInput protocol specification

---

**Note:** This document consolidates technical notes and implementation details. For user-facing documentation, see `README.md`. For release history, see `RELEASE_NOTES.md`.

