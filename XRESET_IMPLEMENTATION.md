# XRESET Implementation Plan

## What is XRESET?

XRESET is the **external reset pin** on the HD6301 microcontroller (pin 10 on 64-pin package). 

**Behavior:**
- When **LOW (0V)**: Holds CPU in reset state
- When **HIGH (5V)**: CPU runs normally
- **Rising edge** (LOW→HIGH): Triggers hardware reset sequence

In the real Atari ST, this pin is connected to the system reset circuitry and can be triggered by:
- Power-on reset
- System reset button (if present)
- MFP (Multi-Function Peripheral) software reset

---

## Current Implementation Status

### ✅ What We Already Have:

1. **CPU Control Functions** (in `6301/cpu.h`):
   ```c
   #define cpu_start()       (cpu.state = RUNNING)
   #define cpu_stop()        (cpu.state = IDLE)
   #define cpu_isrunning()   (cpu.state == RUNNING)
   ```

2. **Reset Function** (in `6301/6301.c`):
   ```c
   hd6301_reset(int Cold);  // 1 = cold reset, 0 = warm reset
   ```

3. **CPU State Checking** (in `6301/6301.c` line 146):
   ```c
   if(!cpu_isrunning()) {
       cpu_start();
   }
   ```

### ❌ What We Need to Add:

1. GPIO pin for XRESET signal
2. Pin initialization and configuration
3. XRESET signal monitoring in main loop
4. Reset state management

---

## Implementation Options

### Option 1: Dedicated GPIO Pin (Most Authentic)

**Hardware Connection:**
```
Atari ST Signal → RP2040 GPIO Pin → HD6301 Emulator
```

**Pros:**
- ✅ Authentic to real hardware
- ✅ Atari ST can control reset directly
- ✅ Works with ST software that uses hardware reset

**Cons:**
- ❌ Requires physical wire connection
- ❌ Uses another GPIO pin (may be limited)

### Option 2: Serial Command (Practical Alternative)

**Command Protocol:**
```
Host sends special byte sequence → RP2040 triggers reset
```

**Pros:**
- ✅ No additional wiring needed
- ✅ Can be triggered from ST software
- ✅ No GPIO pin consumed

**Cons:**
- ❌ Not authentic to real hardware
- ❌ ST software needs modification to use it
- ❌ Adds non-standard protocol

### Option 3: Button-Triggered Reset

**Hardware:**
```
External button → GPIO pin → Reset
```

**Pros:**
- ✅ Easy to implement
- ✅ Useful for debugging/testing
- ✅ Manual control

**Cons:**
- ❌ Not connected to ST
- ❌ Manual operation only

---

## Recommended Implementation (Option 1)

### Step 1: Choose a GPIO Pin

Looking at current pin usage in `config.h`:
- GPIO 0-3: USB (used by TinyUSB)
- GPIO 4-5: UART to ST
- GPIO 8-9: I2C for display
- GPIO 10-14: Joystick 1
- GPIO 16-18: UI buttons
- GPIO 19-22, 26: Joystick 0
- **GPIO 15, 27, 28**: Available

**Recommendation: Use GPIO 15 for XRESET**

### Step 2: Update `config.h`

```c
// GPIO assignment for XRESET pin
#define GPIO_XRESET         15
#define XRESET_ACTIVE_LOW   1   // Set to 1 if ST pulls pin LOW to reset
```

### Step 3: Add XRESET Module (`src/XReset.cpp` and `include/XReset.h`)

**`include/XReset.h`:**
```cpp
#pragma once

#include "pico/stdlib.h"

class XReset {
public:
    static XReset& instance() {
        static XReset inst;
        return inst;
    }

    void init();
    bool is_reset_asserted();
    
private:
    XReset() : last_state_(false), reset_debounce_(0) {}
    
    bool last_state_;
    int reset_debounce_;
};
```

**`src/XReset.cpp`:**
```cpp
#include "XReset.h"
#include "config.h"

void XReset::init() {
    // Configure GPIO pin as input with pull-up
    gpio_init(GPIO_XRESET);
    gpio_set_dir(GPIO_XRESET, GPIO_IN);
    
#if XRESET_ACTIVE_LOW
    // Pull-up: pin is HIGH when not asserted
    gpio_pull_up(GPIO_XRESET);
#else
    // Pull-down: pin is LOW when not asserted
    gpio_pull_down(GPIO_XRESET);
#endif
    
    last_state_ = false;
    reset_debounce_ = 0;
}

bool XReset::is_reset_asserted() {
    bool pin_state = gpio_get(GPIO_XRESET);
    
#if XRESET_ACTIVE_LOW
    // Active low: reset when pin is LOW
    bool reset_active = !pin_state;
#else
    // Active high: reset when pin is HIGH
    bool reset_active = pin_state;
#endif

    // Simple debouncing (requires 3 consecutive readings)
    if (reset_active != last_state_) {
        reset_debounce_++;
        if (reset_debounce_ >= 3) {
            last_state_ = reset_active;
            reset_debounce_ = 0;
        }
    } else {
        reset_debounce_ = 0;
    }
    
    return last_state_;
}
```

### Step 4: Update `6301/6301.h`

Add function to check if CPU should be held in reset:

```c
// Add to function declarations:
void hd6301_set_reset_asserted(int asserted);
int hd6301_is_in_reset();
```

### Step 5: Update `6301/6301.c`

```c
// Add global variable
static int reset_asserted = 0;

void hd6301_set_reset_asserted(int asserted) {
    if (asserted && !reset_asserted) {
        // Reset just asserted - stop CPU
        TRACE("6301: XRESET asserted\n");
        cpu_stop();
        reset_asserted = 1;
    } 
    else if (!asserted && reset_asserted) {
        // Reset just released - perform reset and start CPU
        TRACE("6301: XRESET released - resetting\n");
        hd6301_reset(1);  // Cold reset
        cpu_start();
        reset_asserted = 0;
    }
}

int hd6301_is_in_reset() {
    return reset_asserted;
}

// Modify hd6301_run_clocks to check reset state:
void hd6301_run_clocks(COUNTER_VAR clocks) {
    // Don't run if in reset
    if (reset_asserted) {
        return;
    }
    
    // ... rest of existing code ...
}
```

### Step 6: Update `src/main.cpp`

Add XRESET monitoring to core1 (HD6301 core):

```cpp
#include "XReset.h"

void core1_entry() {
    // Initialise the HD6301
    setup_hd6301();
    hd6301_reset(1);

    unsigned long count = 0;
    absolute_time_t tm = get_absolute_time();
    
    while (true) {
        count += CYCLES_PER_LOOP;
        
        // Check XRESET pin state
        hd6301_set_reset_asserted(XReset::instance().is_reset_asserted());
        
        // Only run CPU if not in reset
        if (!hd6301_is_in_reset()) {
            // Update the tx serial port status based on our serial port handler
            hd6301_tx_empty(1);
            hd6301_run_clocks(CYCLES_PER_LOOP);
        }

        if ((count % 1000000) == 0) {
            //printf("Cycles = %lu\n", count);
        }

        tm = delayed_by_us(tm, CYCLES_PER_LOOP);
        sleep_until(tm);
    }
}

int main() {
    // ... existing initialization ...
    
    // Initialize XRESET monitoring
    XReset::instance().init();
    
    // ... rest of existing code ...
}
```

### Step 7: Update `CMakeLists.txt`

Add the new XReset source file:

```cmake
add_executable(atari_ikbd
    # ... existing files ...
    src/XReset.cpp
    # ... rest of files ...
)
```

---

## Testing XRESET

### Hardware Test Setup:

1. **Connect GPIO 15 to ground** through a button or jumper
2. Power on RP2040
3. Observe OLED display shows normal operation
4. Press button (pull GPIO 15 LOW)
5. Observe display freezes (CPU in reset)
6. Release button (GPIO 15 returns HIGH)
7. Observe display shows splash screen (CPU reset and restarted)

### Software Test:

Add debug output to verify reset behavior:

```cpp
// In XReset.cpp
bool XReset::is_reset_asserted() {
    // ... existing code ...
    
    if (reset_active != last_state_) {
        printf("XRESET state change: %d\n", reset_active);
    }
    
    return last_state_;
}
```

---

## Alternative: Simpler Implementation

If you just want to test reset functionality without full XRESET pin support:

### Quick Button Reset (5-minute implementation):

```cpp
// In main.cpp, add to main loop:
static bool last_button_state = true;
bool button = gpio_get(GPIO_BUTTON_LEFT);  // Use existing button

if (!button && last_button_state) {
    // Button pressed - trigger reset
    hd6301_reset(1);
}
last_button_state = button;
```

This reuses an existing button to trigger reset for testing.

---

## Summary

### What's Needed for Full XRESET Support:

1. ✅ **CPU control functions** - Already exist
2. ✅ **Reset function** - Already exists
3. ⚠️ **GPIO pin** - Need to choose (GPIO 15 recommended)
4. ❌ **Pin initialization** - Need to add
5. ❌ **Reset state management** - Need to add
6. ❌ **Main loop monitoring** - Need to add

### Estimated Implementation Time:

- **Simple button reset**: 5 minutes
- **Full XRESET pin support**: 30-60 minutes
- **Testing and debugging**: 30 minutes

### Files to Modify:

1. `include/config.h` - Add GPIO_XRESET definition
2. `include/XReset.h` - New file (optional, can be simpler)
3. `src/XReset.cpp` - New file (optional)
4. `6301/6301.h` - Add reset control functions
5. `6301/6301.c` - Add reset state management
6. `src/main.cpp` - Add XRESET monitoring
7. `CMakeLists.txt` - Add new source file

---

## Next Steps

Would you like me to:
1. **Implement the full XRESET support** with GPIO pin?
2. **Implement the simple button reset** for quick testing?
3. **Focus on Xbox controller support** instead?

