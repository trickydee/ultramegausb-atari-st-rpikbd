# XRESET and IKBD Reset Command Comparison

## Overview

Comparing how IKBD reset functionality is implemented between:
1. **STEEM SSE** - Atari ST emulator with two IKBD emulation modes
2. **This Project** - RP2040-based physical IKBD replacement using HD6301 emulation

---

## STEEM SSE Implementation

STEEM has **two IKBD emulation modes**:

### 1. High-Level IKBD Emulation (`ikbd.cpp`)
When `OPTION_C1` is disabled, STEEM processes IKBD commands directly in C++ code.

#### Software Reset Command (0x80 0x01):
```cpp
// In ikbd.cpp, line 686-688:
case 0x80:  
  if(src==0x01) 
    ikbd_reset(0);  // 0 = software reset
  break;
```

#### `ikbd_reset(bool hardware_reset)` function:
```cpp
void ikbd_reset(bool hardware_reset) {
  if(hardware_reset) {
    // Hardware reset (power-on or hard reset):
    ikbd_set_clock_to_correct_time();
    Ikbd.command_read_count=0;
    keyboard_buffer_length=0;
    keyboard_buffer[0]=0;
    Ikbd.joy_packet_pos=-1;
    Ikbd.mouse_packet_pos=-1;
    agenda_keyboard_reset(FALSE);
    ZeroMemory(ST_Key_Down,sizeof(ST_Key_Down));
  } else {
    // Software reset (0x80 0x01 command):
    agenda_keyboard_reset(FALSE);
    Ikbd.resetting=1;
    // Schedule reset to complete after 50ms
    agenda_add(agenda_keyboard_reset, milliseconds_to_hbl(50), TRUE);
  }
}
```

#### `agenda_keyboard_reset(int SoftwareReset)`:
This function performs the actual reset actions:

**Hardware Reset (SoftwareReset=FALSE):**
- Reset mouse mode to relative
- Reset joystick mode to auto-notify
- Clear mouse thresholds and scales
- Reset absolute mouse position to screen center
- Clear RAM
- Clear joystick state
- Delete scheduled events

**Software Reset (SoftwareReset=TRUE):**
- Send 0xF1 reset acknowledgment byte to ST
- Send break codes for all "stuck" keys (keys currently pressed)
- Apply game-specific hacks if detected
- Reset certain mode flags

### 2. Low-Level 6301 Emulation
When `OPTION_C1` is enabled and `HD6301_OK` is true, STEEM runs the actual HD6301 ROM firmware in an emulator (similar to this project).

```cpp
#if defined(SSE_HD6301_LL)
  if(OPTION_C1) {
    if(HD6301_OK) {
      TRACE_LOG("6301 reset Ikbd.cpp part\n");
      Ikbd.mouse_upside_down=FALSE;
      return;  // Let the ROM handle the reset
    }
  }
#endif
```

---

## This Project's Implementation

This project **ONLY** uses low-level 6301 emulation - it runs the actual HD6301 ROM firmware.

### Architecture:

```
Atari ST → UART → RP2040 SerialPort → HD6301 emulator → ROM firmware
                                        (sci_in())        (processes commands)
```

### Reset Flow:

1. **Hardware Reset:**
```cpp
// In main.cpp, line 72:
hd6301_reset(1);  // 1 = cold reset

// In 6301.c, line 119:
hd6301_reset(int Cold) {
  crashed = 0;
  cpu_reset();  // Reset CPU state
  if(Cold) {
    memset(ram, 0, 0x80);      // Clear RAM
    memset(iram, 0, NIREGS);   // Clear internal registers
    mouse_x_counter = MOUSE_MASK;  // Reset mouse counters
    mouse_y_counter = MOUSE_MASK;
    // Randomize mouse phase (for game compatibility)
    WORD rnd = rand()%16;
    mouse_x_counter = _rotl(mouse_x_counter, rnd);
    mouse_y_counter = _rotl(mouse_y_counter, rnd);
  }
  iram[TRCSR] = 0x20;  // Set TDRE (Transmit Data Register Empty)
  mem_putw(OCR, 0xFFFF);  // Reset output compare register
}
```

2. **Software Reset (0x80 0x01 command):**
The ROM firmware running in the emulator handles this internally. We don't intercept it in the C code.

```
Atari ST sends: 0x80 0x01
    ↓
SerialPort receives bytes
    ↓
hd6301_receive_byte() → sci_in()
    ↓
ROM firmware receives bytes in RDR register
    ↓
ROM processes command and performs reset
    ↓
ROM sends 0xF1 acknowledgment byte back
```

---

## Key Differences

### STEEM High-Level vs This Project:

| Feature | STEEM (High-Level) | This Project (ROM-based) |
|---------|-------------------|--------------------------|
| **Command Processing** | C++ switch/case in ikbd.cpp | Actual HD6301 ROM firmware |
| **Reset Logic** | Explicit C++ functions | ROM firmware handles internally |
| **Timing** | Scheduled events (50ms delay) | ROM timing (authentic) |
| **Hacks** | Game-specific workarounds | None needed (authentic behavior) |
| **Acknowledgment** | Manually sends 0xF1 | ROM sends 0xF1 naturally |
| **Key Release** | Manually sends break codes | ROM handles key state |

### Implementation Complexity:

**STEEM High-Level:**
- ✅ Full control over behavior
- ✅ Can add game-specific hacks easily
- ❌ Must manually implement all IKBD protocol details
- ❌ May not match authentic timing perfectly

**This Project (ROM-based):**
- ✅ Authentic HD6301 behavior
- ✅ Perfect timing and protocol compliance
- ✅ No need to reimplement IKBD protocol
- ❌ Less visibility into what's happening
- ❌ Harder to add custom features

---

## XRESET Pin (Hardware Reset)

Neither STEEM nor this project currently implement the physical **XRESET** pin behavior.

### What XRESET Should Do:

The HD6301 has an XRESET (external reset) pin that:
1. When pulled LOW, holds the CPU in reset state
2. When released HIGH, causes a hardware reset
3. Reads reset vector from 0xFFFE and starts execution

### In Real Atari ST Hardware:

The XRESET pin is connected to the system reset circuitry and can be triggered by:
- Power-on reset
- Front panel reset button (some ST models)
- Software-initiated reset through the MFP (Multi-Function Peripheral)

### How to Implement in This Project:

```cpp
// Option 1: Add a GPIO pin that the Atari ST can control
#define XRESET_PIN 26  // Example GPIO pin

// In main loop:
if (gpio_get(XRESET_PIN) == 0) {
    // Hold CPU in reset
    hd6301_reset(1);
    cpu_stop();
} else {
    // Resume normal operation
    if (!cpu_isrunning()) {
        cpu_start();
    }
}
```

```cpp
// Option 2: Add a command that host can send
// (Not authentic but practical for RP2040)
if (special_reset_command_received) {
    hd6301_reset(1);
}
```

---

## Xbox Controller Support

The "xbox" part of the branch name suggests adding Xbox controller support for joystick emulation.

### Current Joystick Support:

- USB HID joysticks/gamepads
- GPIO-connected Atari joysticks

### Adding Xbox Controller:

Xbox controllers use a proprietary protocol that's **not standard HID**, but modern Xbox One/Series controllers also support Bluetooth with HID.

**Options:**

1. **USB Xbox Controller (XInput):**
   - Requires special XInput driver (not standard HID)
   - TinyUSB doesn't natively support XInput
   - Would need custom USB class driver

2. **Bluetooth Xbox Controller:**
   - Uses HID over Bluetooth
   - Would need Bluetooth support on RP2040
   - Could use a Bluetooth-to-USB adapter

3. **Xbox 360/One Controller via USB:**
   - Some report as HID devices in "HID mode"
   - May work with existing HID joystick code
   - Needs testing

**Recommendation:** Start by testing if Xbox controllers work with existing HID code. Many modern gamepads (including some Xbox controllers) support standard HID game controller protocol.

---

## Summary

### Reset Comparison:

1. **STEEM**: Implements reset in high-level C++ OR runs ROM
2. **This Project**: Only runs ROM (authentic behavior)
3. **XRESET Pin**: Not currently implemented in either

### Next Steps for "xreset-and-xbox" Branch:

1. **XRESET Implementation:**
   - Add GPIO pin for hardware reset
   - OR add serial command for reset trigger
   - Test with ST software that uses reset

2. **Xbox Controller Support:**
   - Test current HID code with Xbox controllers
   - Add XInput support if needed
   - Map Xbox buttons to Atari joystick actions

---

## References

- STEEM SSE source: `ikbd.cpp`, lines 686-1285
- HD6301 datasheet: Reset pin behavior (XRESET)
- TinyUSB documentation: HID host support
- Xbox Controller protocols: XInput vs HID modes




