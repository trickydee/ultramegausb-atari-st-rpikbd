# Quality of Life Keyboard Shortcuts

## New Shortcuts Added (v5.1.0)

### Keypad Key Access
Modern keyboards often lack dedicated keypads or have different layouts. These shortcuts provide easy access to Atari ST keypad keys:

- **Alt + [** → Atari Keypad **/** (Division)
  - Sends scancode 101 (Atari keypad divide key)
  
- **Alt + ]** → Atari Keypad **\*** (Multiplication)
  - Sends scancode 102 (Atari keypad multiply key)

### Joystick Source Toggle
Toggle joystick input sources between D-SUB (GPIO) and USB controllers without using the OLED buttons:

- **Ctrl + F9** → Toggle **Joystick 0** (D-SUB ↔ USB)
  - Same function as pressing left/right buttons on PAGE_JOY0
  - Toggles between physical D-SUB port and USB controllers
  
- **Ctrl + F10** → Toggle **Joystick 1** (D-SUB ↔ USB)
  - Same function as pressing left/right buttons on PAGE_JOY1
  - Toggles between physical D-SUB port and USB controllers

### Caps Lock Toggle
Proper Caps Lock toggle behavior like modern keyboards:

- **Caps Lock** → Toggle Caps Lock state (press once = ON, press again = OFF)
  - Sends persistent state to Atari ST (not just a momentary key press)
  - Works like a modern keyboard's Caps Lock toggle
  - State persists until toggled again

## Complete Keyboard Shortcut Reference

### System Control
- **Ctrl + F12** → Toggle mouse/joystick 0 mode
- **Ctrl + F11** → XRESET (HD6301 hardware reset)
- **Alt + +** → Set CPU to 270MHz (overclock)
- **Alt + -** → Set CPU to 150MHz (default)

### Joystick Control
- **Ctrl + F9** → Toggle Joystick 0 source (D-SUB/USB) [NEW]
- **Ctrl + F10** → Toggle Joystick 1 source (D-SUB/USB) [NEW]

### Special Keys
- **Alt + /** → Atari INSERT key
- **Alt + [** → Atari Keypad / [NEW]
- **Alt + ]** → Atari Keypad * [NEW]
- **Caps Lock** → Toggle Caps Lock state (persistent) [NEW]

## Implementation Details

### Technical Notes
1. All shortcuts use **debouncing** to prevent repeated triggers
2. Shortcut keys are **blocked** from being sent to the Atari ST
3. **Settings are saved** to flash when joystick sources are toggled
4. **UI updates** automatically to reflect changes

### Code Changes
- `include/UserInterface.h` - Added `toggle_joystick_source()` method
- `src/UserInterface.cpp` - Implemented joystick toggle function
- `src/HidInput.cpp` - Added keyboard shortcut detection and handling

### Scancode Reference
- Atari Keypad `/` = scancode 101 (HID 0x54)
- Atari Keypad `*` = scancode 102 (HID 0x55)
- HID `F9` = 0x42
- HID `F10` = 0x43
- HID `[` = 0x2F (HID_KEY_BRACKET_LEFT)
- HID `]` = 0x30 (HID_KEY_BRACKET_RIGHT)

## Usage Examples

### Gaming Setup
1. Connect PS4 controller to USB
2. Press **Ctrl+F9** to assign it to Joystick 1
3. Connect another USB controller
4. Press **Ctrl+F10** to assign it to Joystick 0
5. Play two-player games!

### Calculator/Numeric Applications
When using Atari ST calculator or spreadsheet apps:
- **Alt+[** for division
- **Alt+]** for multiplication
- No need to reach for separate numeric keypad

### Quick Reset
- **Ctrl+F11** to reset the IKBD emulator without power cycling
- Useful for testing or recovering from configuration issues

## Future Enhancements

Potential QoL improvements for future versions:
- Save/load controller mappings
- Configurable deadzone via keyboard
- Button remapping
- Macro support
- OLED timeout configuration

