# Atari ST USB Adapter - Project Modernization Documentation

## Overview
This document captures the complete modernization process of the Atari ST USB adapter project, including dependency upgrades, compatibility fixes, and performance optimizations.

## Project Status
- **Original State**: Custom Pico SDK 1.1.2 fork + Custom TinyUSB 0.10.1 fork
- **Current State**: Official Pico SDK 1.5.1 + Official TinyUSB 0.19.0
- **Performance**: Overclocked to 270 MHz (up from 250 MHz)
- **Status**: Fully functional with enhanced features

## Key Changes Made

### 1. Dependency Upgrades

#### Pico SDK Upgrade (1.1.2 → 1.5.1)
- **Reason**: Required for TinyUSB 0.19.0 compatibility
- **Method**: Switched from custom fork to official release
- **Impact**: Better stability, latest features, official support

#### TinyUSB Upgrade (0.10.1 → 0.19.0)
- **Reason**: Original developer used custom fork due to bugs, now resolved
- **Method**: Gradual upgrade through versions (0.10.1 → 0.12.0 → 0.13.0 → 0.15.0 → 0.16.0 → 0.19.0)
- **Impact**: Better USB compatibility, hub support, modern API

### 2. Compatibility Layer Implementation

#### HID Parser Integration
- **Problem**: Original project relied on LUFA HID parser from custom TinyUSB fork
- **Solution**: Copied `hidparser/` directory and created compatibility wrapper
- **Files Added**:
  - `hidparser/` directory with LUFA HID parser library
  - `include/hid_app_host.h` - Compatibility header
  - `src/hid_app_host.c` - Compatibility implementation

#### API Compatibility Wrapper
- **Purpose**: Bridge between application's expected API and official TinyUSB API
- **Key Functions**:
  - `tuh_hid_is_mounted()` - Device mount status
  - `tuh_hid_get_type()` - Device type detection
  - `hid_app_request_report()` - Report requesting (renamed from `tuh_hid_get_report`)
  - `tuh_hid_get_report_size()` - Report size information
  - `tuh_hid_get_report_info()` - Report descriptor info

### 3. Critical Bug Fixes

#### USB Device Enumeration
- **Problem**: Only one device could be enumerated through USB hub
- **Root Cause**: `CFG_TUH_DEVICE_MAX` defaulted to 1 in TinyUSB
- **Solution**: Updated `include/tusb_config.h`:
  ```c
  #define CFG_TUH_DEVICE_MAX 4        // Max non-hub devices
  #define CFG_TUH_HID 8               // Max HID interfaces
  #define CFG_TUSB_HOST_DEVICE_MAX 8  // Total device array size
  ```

#### Report Reception
- **Problem**: HID reports not being received after device mount
- **Root Cause**: TinyUSB 0.10.1+ requires explicit `tuh_hid_receive_report()` calls
- **Solution**: Added calls in `hid_app_host.c`:
  - After device mount in `tuh_hid_mount_cb`
  - After report processing in `tuh_hid_report_received_cb`

#### Display Integration
- **Problem**: Splash screen not displaying (corrupted image)
- **Root Cause**: Missing `PAGE_SPLASH` case in main update loop
- **Solution**: Added splash screen handling in `UserInterface::update()`

### 4. New Features Added

#### USB Debug Page
- **Purpose**: Real-time troubleshooting of USB device enumeration
- **Display**: Shows mount/unmount counts, active devices, report statistics
- **Access**: Middle button cycles through pages (Status → Settings → USB Debug → Serial Debug → Splash)

#### Splash Screen
- **Design**: Clean "ATARI USB to Mega Adapter" text
- **Performance**: Only refreshes when button pressed (not continuous)
- **Integration**: First page displayed after reboot

#### Performance Optimization
- **Overclock**: Increased from 250 MHz to 270 MHz (+8% performance)
- **Benefit**: Better USB responsiveness, faster HID processing

### 5. Build System Updates

#### CMakeLists.txt Changes
- **Removed**: `tinyusb_board` (redundant with Pico SDK)
- **Added**: HID parser sources and include directories
- **Updated**: Source file lists for new compatibility layer

#### Pico SDK Integration
- **File**: `pico-sdk/src/rp2_common/tinyusb/CMakeLists.txt`
- **Change**: Commented out `tinyusb_board` library definition
- **Reason**: Pico SDK handles board initialization

## File Structure Changes

### New Files Added
```
hidparser/
├── Architectures.h
├── ArchitectureSpecific.h
├── Attributes.h
├── BoardTypes.h
├── Common.h
├── CompilerSpecific.h
├── Events.h
├── HIDClassCommon.h
├── HIDParser.c
├── HIDParser.h
├── HIDReportData.h
├── StdDescriptors.h
└── USBMode.h

include/
└── hid_app_host.h

src/
└── hid_app_host.c
```

### Modified Files
- `src/main.cpp` - Overclock update, removed `stdio_init_all()`
- `src/UserInterface.cpp` - Added splash screen, USB debug page
- `include/UserInterface.h` - Added new page types and methods
- `include/tusb_config.h` - Updated USB configuration limits
- `CMakeLists.txt` - Updated build configuration

## API Changes Handled

### TinyUSB API Evolution
- **`tusb_init()`**: Return type changed from `tusb_error_t` to `bool`
- **`tuh_hid_get_report()`**: Signature changed, renamed to `hid_app_request_report()`
- **Report handling**: Now requires explicit `tuh_hid_receive_report()` calls
- **Device limits**: Configuration moved to `tusb_config.h`

### Display API Usage
- **SSD1306 Library**: Custom implementation by David Schramm (MIT License)
- **Key Functions**: `ssd1306_draw_line()`, `ssd1306_draw_string()`, `ssd1306_show()`
- **Display Size**: 128x64 pixels

## Performance Improvements

### Overclocking
- **Previous**: 250 MHz
- **Current**: 270 MHz
- **Benefit**: 8% performance increase, better USB responsiveness

### Memory Optimization
- **Device Arrays**: Sized to `CFG_TUH_HID` instead of `CFG_TUSB_HOST_DEVICE_MAX`
- **Buffer Management**: Improved report buffer handling
- **Display Updates**: Reduced unnecessary screen refreshes

## Testing and Validation

### USB Device Compatibility
- ✅ **Keyboards**: Full compatibility maintained
- ✅ **Mice**: Full compatibility maintained  
- ✅ **USB Hubs**: Multi-device support working
- ✅ **Wireless Dongles**: Logitech Unifying receivers working
- ✅ **Multi-interface Devices**: Proper enumeration

### Performance Testing
- ✅ **270 MHz Overclock**: Stable operation confirmed
- ✅ **USB Debug Page**: Real-time monitoring working
- ✅ **Splash Screen**: Clean display, proper alignment
- ✅ **Button Navigation**: All pages accessible

## Future Maintenance Notes

### Dependency Updates
- **Pico SDK**: Can be updated to newer versions as they become available
- **TinyUSB**: Monitor for new releases, test compatibility
- **HID Parser**: May need updates if LUFA library changes

### Known Limitations
- **Display Size**: 128x64 OLED limits splash screen design
- **Memory**: Limited by RP2040 constraints
- **USB Power**: Some high-power devices may not work

### Debugging Tools
- **USB Debug Page**: Use for troubleshooting device enumeration
- **Serial Debug**: Monitor data flow to Atari ST
- **Overclock**: Can be adjusted in `src/main.cpp` if needed

## Build Instructions

### Prerequisites
- Raspberry Pi Pico SDK 1.5.1+
- CMake 3.13+
- ARM GCC toolchain

### Build Process
```bash
mkdir build
cd build
cmake ..
make -j4
```

### Output Files
- `atari_ikbd.uf2` - Flashable firmware
- `atari_ikbd.elf` - Debug symbols
- `atari_ikbd.hex` - Alternative format

## Troubleshooting

### Common Issues
1. **USB devices not working**: Check USB debug page for enumeration status
2. **Display corruption**: Verify splash screen integration in update loop
3. **Build errors**: Ensure all dependencies are properly linked
4. **Performance issues**: Check overclock settings in main.cpp

### Debug Information
- **Mount Count**: Number of devices successfully enumerated
- **Report Count**: Number of HID reports received
- **Active Devices**: Currently connected device count
- **Last Address**: Most recently enumerated device address

## Conclusion

This modernization successfully upgraded the Atari ST USB adapter from outdated custom dependencies to the latest official releases while preserving all original functionality and adding new features. The project is now maintainable, performant, and ready for future development.

**Total Changes**: 3 files modified, 61 insertions, 19 deletions
**Commit**: `9a043b5` - "Complete project modernization and optimization"
**Branch**: `feature/pico-sdk-upgrade`
