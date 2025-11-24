# Bluepad32 Bluetooth Gamepad Integration

## Status: ✅ Bluetooth Pairing Working! (v18.0.1-blu)

### Major Success! 🎉
**Bluetooth gamepad pairing is now working!** Xbox controllers can be detected and paired successfully.

### Completed ✅
1. ✅ Added Bluepad32 as git submodule
2. ✅ Created custom platform implementation (`src/bluepad32_platform.c`)
3. ✅ Created Bluepad32 to Atari ST converter (`src/bluepad32_atari.cpp`)
4. ✅ Wired Bluepad32 into `handle_joystick()` system
5. ✅ Added conditional compilation guards
6. ✅ **Build System Integration (CMakeLists.txt)** - Conditionally builds for Pico 2 W
7. ✅ **Main Loop Integration** - Uses `async_context_poll()` for non-blocking btstack integration
8. ✅ **Initialization Code** - Separated to `src/bluepad32_init.c` to avoid HID conflicts
9. ✅ **OLED Display Enabled** - For debugging and status display
10. ✅ **Diagnostic Logging** - Device discovery logging added
11. ✅ **HCI Initialization Delay** - 2-second delay before starting scanning
12. ✅ **Critical Fix: Initialization Order** - CYW43 initialized BEFORE I2C/SPI peripherals
13. ✅ **Clock Speed Reduction** - 150 MHz for Bluetooth builds (improves CYW43 stability)
14. ✅ **CYW43 Reset Sequence** - Power cycle before initialization

### Key Fix That Made It Work

**Problem:** I2C/SPI initialization before CYW43 was interfering with wireless pin configuration, causing STALL timeouts.

**Solution:** Reordered initialization sequence:
1. Set clock speed (150 MHz for Bluetooth)
2. Initialize CYW43/Bluepad32 **FIRST**
3. Then initialize OLED display (I2C) **AFTER**

This matches the issue reported in: https://forums.pimoroni.com/t/plasma-2350-w-wifi-problems/26810

### Current Issues ⚠️

**Version:** 18.0.1-blu (Pico 2 W build)

**Major Issues:**
1. **XIP causing USB/serial link issues**: With XIP enabled, serial communication from Pico to ST is not working properly. Can see ST→Pico messages but not Pico→ST. This was seen in previous XIP test builds. May need to switch to `copy_to_ram` or use hybrid approach.

**Minor Issues:**
2. **Joystick indicator not incrementing**: When Bluetooth gamepad is detected and paired, the joystick count on the OLED display doesn't increment. Need to wire up `bluepad32_get_connected_count()` to the UI update logic.

**Non-Critical:**
- CLM firmware warnings still appear but don't block functionality
- Some CYW43 STALL timeouts may still occur but don't prevent pairing

### Implementation Details

#### Build System (CMakeLists.txt)
- Detects Pico 2 W boards (`pico2_w`)
- Conditionally adds Bluepad32 source files
- Links Bluepad32 and btstack libraries
- **Uses XIP (Execute In Place) for Bluetooth builds** - saves RAM but may cause performance issues
- Output: `atari_ikbd_pico2_w.uf2`

#### Initialization Sequence (FIXED ORDER)
1. Initialize async_context for btstack
2. Set async_context for CYW43 (before `cyw43_arch_init()`)
3. **Initialize CYW43 chip FIRST** (`cyw43_arch_init()`) - **CRITICAL: Before I2C/SPI**
4. Set custom platform (`uni_platform_set_custom()`)
5. Initialize btstack run loop
6. Initialize Bluepad32 (`uni_init()`)
7. Wait 2 seconds for HCI initialization
8. Start Bluetooth scanning
9. **Then initialize OLED display (I2C)** - After CYW43

#### Main Loop Integration
- `bluepad32_poll()` called every 10ms in main loop
- Non-blocking - keeps Core 1 dedicated to 6301 emulator
- Uses `async_context_poll()` for btstack integration

### Test Results

**Successful Pairing:**
- Xbox Wireless Controller (firmware v5.x) - ✅ Working
- Device detected via BLE
- Pairing completes successfully
- Device information retrieved correctly
- HID service connected

**Log Output:**
```
Device found: 44:16:22:40:F4:99 (public), appearance 0x3c4 / COD 0x508
BT Device discovered: addr=44:16:22:40:F4:99, name='', COD=0x0508, RSSI=209
  -> Accepting device (will attempt connection)
Pairing complete, success
Device detected as Xbox Wireless: 0x20
Device 44:16:22:40:F4:99 is connected
bluepad32_platform: device connected: 0x200093f8
bluepad32_platform: device ready: 0x200093f8
```

### Files Created

1. `src/bluepad32_platform.c` - Custom platform implementation
2. `include/bluepad32_platform.h` - Public API header
3. `src/bluepad32_atari.cpp` - Gamepad to Atari converter
4. `src/bluepad32_init.c` - Initialization code (separated to avoid HID conflicts)
5. `include/bluepad32_init.h` - Initialization header
6. `src/btstack_config.h` - BTstack configuration
7. `src/sdkconfig.h` - Bluepad32 SDK configuration
8. `docs/BLUEPAD32_INTEGRATION.md` - This file
9. `docs/BLUETOOTH_DEBUGGING_NOTES.md` - Debugging notes

### Files Modified

1. `src/HidInput.cpp` - Added Bluepad32 integration
2. `src/main.cpp` - Added Bluepad32 initialization and polling, fixed initialization order
3. `CMakeLists.txt` - Added Bluepad32 build configuration, XIP for Bluetooth builds
4. `include/version.h` - Updated to 18.0.1-blu
5. `src/bluepad32_init.c` - Added CYW43 reset sequence, clock speed handling
6. `src/bluepad32_platform.c` - Added diagnostic logging

### Next Steps

1. **Fix XIP/USB serial issue** - Consider switching to `copy_to_ram` or hybrid approach
2. **Wire up joystick count** - Add `bluepad32_get_connected_count()` to UI update logic
3. **Test other controllers** - PS5, Switch Pro, etc.
4. **Verify gamepad input** - Test in actual games
5. **Performance testing** - Verify 6301 emulator performance with XIP

### References

- [Bluepad32 Documentation](https://bluepad32.readthedocs.io/)
- [Pico W Example](https://github.com/ricardoquesada/bluepad32/tree/main/examples/pico_w)
- [BTstack Documentation](https://github.com/bluekitchen/btstack)
- [Pimoroni Forum - SPI/CYW43 Conflict](https://forums.pimoroni.com/t/plasma-2350-w-wifi-problems/26810)
