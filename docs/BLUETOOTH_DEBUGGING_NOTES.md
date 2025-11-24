# Bluetooth Debugging Notes

## Current Status (v18.0.1-blu)

### Build Configuration
- **Board**: Pico 2 W (RP2350, 512KB RAM)
- **Binary Type**: XIP (Execute In Place from flash)
- **OLED Display**: Enabled
- **Serial Logging**: Enabled
- **Version**: 18.0.1-blu

### Initialization Sequence
1. CYW43 chip initialization (`cyw43_arch_init()`)
2. Bluepad32 platform setup
3. btstack run loop initialization
4. Bluepad32 initialization (`uni_init()`)
5. 2-second delay (to allow HCI initialization)
6. Bluetooth scanning starts

### Serial Console Output (Expected)
```
Initializing CYW43 (WiFi/Bluetooth chip)...
CYW43 initialized successfully
Note: CLM firmware warnings may appear but are often non-critical for Bluetooth
Bluepad32 initialized - scanning for Bluetooth gamepads...
bluepad32_platform: on_init_complete()
Clearing stored Bluetooth keys...
Waiting for HCI to be ready...
Starting Bluetooth scanning and autoconnect...
Bluetooth scanning started - waiting for devices...
Put your controller in pairing mode now!
```

### Current Issues

#### 1. CYW43 STALL Timeouts
**Symptoms:**
- Multiple `[CYW43] STALL(0;1-1): timeout` errors
- `[CYW43] do_ioctl(2, 263, 1008): timeout`
- `[CYW43] error: hdr mismatch` errors

**Impact:** CYW43 chip not responding to commands, blocking Bluetooth functionality.

**Possible Causes:**
- Hardware communication issue
- Power supply instability
- CYW43 chip in bad state
- Timing issues

#### 2. CLM Load Failure
**Symptoms:**
- `[CYW43] CLM load failed`

**Impact:** Country List Management firmware not loaded. May block Bluetooth initialization.

**Note:** Some sources suggest CLM is mainly for WiFi regulatory compliance, but it may be required for Bluetooth.

#### 3. HCI Not Ready
**Symptoms:**
- `HCI not ready, cannot send packet, will again try later. Current state idx=0`
- `HCI not ready, cannot send packet, will again try later. Current state idx=1`

**Impact:** HCI layer not fully initialized, preventing Bluetooth operations.

**Attempted Fix:** Added 2-second delay before starting scanning.

#### 4. No Device Discovery
**Symptoms:**
- No "BT Device discovered" messages when controllers are in pairing mode
- Controllers not being found

**Impact:** Cannot pair controllers.

**Possible Causes:**
- HCI not ready (see issue #3)
- Scanning not working due to CYW43 issues
- Bluetooth stack not fully initialized

### Diagnostic Logging Added

1. **Device Discovery Logging:**
   - Logs all discovered Bluetooth devices
   - Shows address, name, COD, RSSI
   - Indicates if device is accepted or rejected

2. **Initialization Logging:**
   - Clear messages for each initialization step
   - Status updates during scanning setup

### Files Modified

- `src/bluepad32_init.c` - Bluepad32 initialization
- `src/bluepad32_platform.c` - Platform callbacks with diagnostic logging
- `src/bluepad32_atari.cpp` - Gamepad to Atari ST converter
- `include/version.h` - Version updated to 18.0.1-blu
- `CMakeLists.txt` - Build configuration for Pico 2 W

### Next Steps

1. **Test v18.0.1-blu build:**
   - Flash and observe serial console
   - Try pairing Xbox/PS5 controllers
   - Check for "BT Device discovered" messages

2. **If issues persist:**
   - Check hardware connections to CYW43 chip
   - Verify power supply stability
   - Investigate CYW43 chip reset requirements
   - Check if CLM failure is blocking Bluetooth

3. **Potential Solutions:**
   - Add CYW43 chip reset before initialization
   - Increase delay before scanning
   - Check CYW43 firmware version
   - Investigate hardware communication timing
   - Consider power supply improvements

### References

- [Bluepad32 Documentation](https://bluepad32.readthedocs.io/)
- [Pico W Example](https://github.com/ricardoquesada/bluepad32/tree/main/examples/pico_w)
- [CYW43 Driver Documentation](https://github.com/raspberrypi/pico-sdk/tree/master/lib/cyw43-driver)

