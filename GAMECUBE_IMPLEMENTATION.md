# GameCube Controller USB Adapter Support

**Date Started:** October 31, 2025  
**Branch:** gamecube-controller-usb-adapter-support  
**Base Version:** 10.0.0  
**Status:** 🚧 Planning

---

## 🎯 Goal

Add support for **Nintendo GameCube Controllers** connected via USB adapter to the Atari ST USB Adapter.

---

## 📋 GameCube USB Adapter Overview

### Common Adapters:
1. **Official Nintendo GameCube Controller Adapter for Wii U/Switch**
   - Most common adapter
   - VID: 0x057E (Nintendo)
   - PID: 0x0337
   - Supports up to 4 GameCube controllers
   - Two USB modes: "Wii U" and "PC"

2. **Third-Party Adapters**
   - Mayflash GameCube Adapter
   - 8BitDo GameCube Adapter
   - Various generic adapters

### GameCube Controller Features:
- **Analog Stick** (main control stick)
- **C-Stick** (second analog stick)
- **D-Pad** (8-direction)
- **8 Buttons:**
  - A, B, X, Y (face buttons)
  - L, R (shoulder buttons/triggers - analog)
  - Z (trigger button)
  - START
- **Analog Triggers:** L and R have analog pressure sensing

---

## 🔍 Research Questions

1. **Which adapter do you have?**
   - Official Nintendo adapter?
   - Third-party adapter (brand/model)?

2. **USB Mode:**
   - Does it have a switch for "Wii U" vs "PC" mode?
   - Which mode are you using?

3. **VID/PID:**
   - What does your Mac show for the adapter?
   - Does it appear as one device or multiple?

4. **Current Behavior:**
   - Does it detect at all currently?
   - What does the OLED show when you plug it in?

---

## 🛠️ Implementation Approach

### Option A: Official Nintendo Adapter
The official adapter uses a **non-standard USB protocol** that requires:
- Custom driver/library (libusb-based)
- Direct USB control transfers
- May be challenging with TinyUSB HID stack

### Option B: Third-Party "HID Mode" Adapter
Some adapters present as standard HID joysticks:
- Easier integration (like other controllers)
- Standard HID report parsing
- Should work with existing infrastructure

### Option C: Adapter in "Switch Mode"
Some adapters emulate Switch Pro Controllers:
- May already work with existing Switch controller code!
- Worth testing first

---

## 📝 Implementation Plan

### Phase 1: Discovery
- [ ] Identify adapter brand/model
- [ ] Get VID/PID from hardware
- [ ] Check current detection behavior
- [ ] Determine USB protocol (HID vs custom)
- [ ] Check if it already works as Switch controller

### Phase 2: Detection & Initialization
- [ ] Add VID/PID detection
- [ ] Create gamecube_controller.h
- [ ] Create gamecube_controller.c
- [ ] Implement mount callback
- [ ] Test detection with OLED

### Phase 3: Report Parsing
- [ ] Enable debug to see raw reports
- [ ] Map analog stick
- [ ] Map C-stick (or ignore for Atari)
- [ ] Map D-Pad
- [ ] Map face buttons (A, B, X, Y)
- [ ] Map triggers (L, R, Z)

### Phase 4: Integration
- [ ] Add to hid_app_host.c
- [ ] Add to HidInput.cpp
- [ ] Implement gamecube_to_atari() mapping
- [ ] Add to CMakeLists.txt

### Phase 5: Testing & Polish
- [ ] Test all controls
- [ ] Refine deadzone
- [ ] Create splash screen
- [ ] Update README
- [ ] Bump version

---

## 🎮 Expected Challenges

### Challenge 1: Non-Standard Protocol
Official Nintendo adapter doesn't use standard HID:
- Requires custom USB control transfers
- May need libusb-style approach
- TinyUSB may not support this directly

**Solution:** Use third-party adapter with HID mode, or implement custom USB handling

### Challenge 2: Multiple Controller Support
Adapters support 4 controllers:
- Need to handle multiple devices from one adapter
- May appear as 4 separate HID interfaces
- Need to map to Atari joystick 0 or 1

**Solution:** Detect first connected controller, map to Joy 0 or 1

### Challenge 3: Analog Triggers
GameCube has analog L/R triggers:
- Need to decide threshold for "pressed"
- Z button is digital, easier to map

**Solution:** Use 50% threshold for L/R, or just use Z for fire

---

## 🎯 Next Steps

**When Ready:**
1. Plug in GameCube USB adapter (with controller connected)
2. Enable debug: `DEBUG=1 ./build-all.sh`
3. Check OLED for VID/PID
4. Report adapter details here
5. Test if it already works as Switch Pro Controller
6. Proceed with implementation

---

**Status:** Awaiting hardware information  
**Estimated Effort:** 2-6 hours (depends on adapter type)

