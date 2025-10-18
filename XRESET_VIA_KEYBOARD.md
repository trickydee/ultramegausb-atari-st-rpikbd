# XRESET via Keyboard Shortcut

## Overview

Trigger HD6301 hardware reset using a USB keyboard shortcut - **much more practical** than a GPIO pin!

## Advantages

✅ **No extra wiring** - Uses existing USB keyboard
✅ **User-friendly** - Easy to trigger reset
✅ **Authentic behavior** - Performs real hardware reset
✅ **Already have infrastructure** - Keyboard shortcut handling exists
✅ **Debugging tool** - Useful for testing and development

## Recommended Keyboard Shortcut

**Ctrl+Alt+Delete** - The universal reset combination

Alternative options:
- Ctrl+Alt+Backspace (Linux standard)
- Ctrl+Alt+R (simpler, R for Reset)
- Ctrl+Shift+Delete

## Implementation

### Challenge: Cross-Core Communication

The keyboard handler runs on **Core 0**, but the HD6301 emulator runs on **Core 1**.

We need a way to signal the reset between cores. Options:

1. **Global flag** (simple, but needs careful handling)
2. **Multicore FIFO** (Pico SDK built-in)
3. **Direct function call** (if thread-safe)

**Recommended: Use a global atomic flag**

---

## Step-by-Step Implementation

### Step 1: Add Global Reset Flag

**In `include/config.h`:**

```cpp
// XRESET trigger flag (set by keyboard handler, checked by HD6301 core)
extern volatile bool g_xreset_triggered;
```

**In `src/main.cpp`:**

```cpp
// Global flag for XRESET trigger (volatile for cross-core communication)
volatile bool g_xreset_triggered = false;
```

### Step 2: Add Reset Function to 6301 Interface

**In `6301/6301.h`:**

```c
// Trigger a hardware reset (can be called from any core)
void hd6301_trigger_reset();
```

**In `6301/6301.c`:**

```c
// Add global variable
static volatile int pending_reset = 0;

void hd6301_trigger_reset() {
    TRACE("6301: Reset triggered externally\n");
    pending_reset = 1;
}

// Modify hd6301_run_clocks to check for pending reset:
void hd6301_run_clocks(COUNTER_VAR clocks) {
    // Check for pending reset request
    if (pending_reset) {
        TRACE("6301: Executing pending reset\n");
        pending_reset = 0;
        hd6301_reset(1);  // Cold reset
        return;  // Don't run cycles this iteration
    }
    
    // ... rest of existing code ...
    
    // make sure our 6301 is running OK
    if(!cpu_isrunning())
    {
        TRACE("6301 starting cpu\n");
        cpu_start();
    }
    
    // ... rest of code unchanged ...
}
```

### Step 3: Add Keyboard Shortcut Handler

**In `src/HidInput.cpp`:**

Add this code after the existing keyboard shortcuts (around line 216):

```cpp
// Check for Ctrl+Alt+Delete to trigger XRESET
static bool last_reset_state = false;
bool delete_pressed = false;
for (int i = 0; i < 6; ++i) {
    if (kb->keycode[i] == HID_KEY_DELETE) {
        delete_pressed = true;
        break;
    }
}
if (ctrl_pressed && alt_pressed && delete_pressed) {
    if (!last_reset_state) {
        // Trigger HD6301 hardware reset
        extern void hd6301_trigger_reset();
        hd6301_trigger_reset();
        
        // Optional: Visual feedback on display
        // (Could flash "RESETTING..." on OLED)
        
        last_reset_state = true;
    }
} else {
    last_reset_state = false;
}

// Make sure Ctrl+Alt+Delete doesn't get sent to Atari ST
if (ctrl_pressed && alt_pressed && delete_pressed) {
    // Block this key combo from being sent
    for (int i = 0; i < 6; ++i) {
        if (kb->keycode[i] == HID_KEY_DELETE) {
            st_keys[i] = 0;  // Don't send to ST
        }
    }
}
```

### Step 4: Update Includes

**In `src/HidInput.cpp`** (top of file):

```cpp
#include "HidInput.h"
#include "6301.h"  // Add this for hd6301_trigger_reset()
// ... rest of existing includes ...
```

### Step 5: Optional - Add Visual Feedback

**In `include/UserInterface.h`:**

```cpp
class UserInterface {
public:
    // ... existing functions ...
    
    void show_reset_message();  // New function
    
    // ... rest of class ...
};
```

**In `src/UserInterface.cpp`:**

```cpp
void UserInterface::show_reset_message() {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 20, 24, 2, (char*)"RESET");
    ssd1306_draw_string(&disp, 20, 44, 1, (char*)"Ctrl+Alt+Del");
    ssd1306_show(&disp);
    dirty = true;
}
```

**Then in `HidInput.cpp` keyboard handler:**

```cpp
if (ctrl_pressed && alt_pressed && delete_pressed) {
    if (!last_reset_state) {
        // Show visual feedback
        ui_->show_reset_message();
        
        // Small delay for user to see message
        sleep_ms(500);
        
        // Trigger reset
        extern void hd6301_trigger_reset();
        hd6301_trigger_reset();
        
        last_reset_state = true;
    }
}
```

---

## Complete Code Changes Summary

### Files to Modify:

1. **`6301/6301.h`** - Add `hd6301_trigger_reset()` declaration
2. **`6301/6301.c`** - Add reset trigger implementation
3. **`src/HidInput.cpp`** - Add keyboard shortcut handler
4. **`include/UserInterface.h`** - Optional: Add visual feedback function
5. **`src/UserInterface.cpp`** - Optional: Implement visual feedback

### Total Lines of Code: ~40 lines

---

## Testing

### Test Procedure:

1. Build and flash firmware
2. Connect USB keyboard
3. Boot Atari ST
4. Wait for IKBD to initialize
5. Press **Ctrl+Alt+Delete**
6. Observe:
   - OLED shows splash screen (reset occurred)
   - ST may show "Keyboard Error" or restart if OS detects IKBD reset
   - All key states cleared
   - Mouse/joystick modes reset to defaults

### Expected Behavior:

**What happens when you press Ctrl+Alt+Delete:**

1. Keyboard handler detects key combination
2. `hd6301_trigger_reset()` sets pending reset flag
3. HD6301 emulator (core 1) checks flag on next cycle
4. Executes `hd6301_reset(1)` - cold reset:
   - CPU resets to reset vector
   - RAM cleared
   - Internal registers cleared
   - Mouse counters reset
   - ROM execution restarts
5. HD6301 ROM firmware initializes
6. Sends 0xF1 reset acknowledgment to ST
7. ST IKBD driver reinitializes

### Debug Output:

Enable tracing to see reset in action:

```cpp
// In 6301/6301.c
#define TRACE_6301
```

You should see:
```
6301: Reset triggered externally
6301: Executing pending reset
6301 emu cpu reset (cold 1)
```

---

## Alternative Keyboard Shortcuts

If Ctrl+Alt+Delete conflicts with something, use one of these:

### Ctrl+Alt+Backspace:
```cpp
if (kb->keycode[i] == HID_KEY_BACKSPACE)
```

### Ctrl+Alt+R (R for Reset):
```cpp
if (kb->keycode[i] == HID_KEY_R)
```

### Ctrl+Shift+Delete:
```cpp
bool shift_pressed = (kb->modifier & KEYBOARD_MODIFIER_LEFTSHIFT) || 
                     (kb->modifier & KEYBOARD_MODIFIER_RIGHTSHIFT);
if (ctrl_pressed && shift_pressed && delete_pressed)
```

---

## Comparison with GPIO Pin Method

| Feature | Keyboard Shortcut | GPIO Pin |
|---------|------------------|----------|
| **Wiring** | None needed | Extra wire to ST |
| **User Control** | Easy (keyboard) | Need physical access |
| **ST Integration** | Works immediately | Needs ST modification |
| **Authentic** | Functionally yes | Hardware authentic |
| **Debugging** | Excellent | Good |
| **Implementation** | 40 lines | 150+ lines |

**Verdict: Keyboard shortcut is the winner for practicality!**

---

## Future Enhancements

### 1. Double-Tap Protection:

Prevent accidental resets:

```cpp
static absolute_time_t last_reset_time = 0;
absolute_time_t now = get_absolute_time();

if (absolute_time_diff_us(last_reset_time, now) < 2000000) {
    // Less than 2 seconds since last reset - ignore
    return;
}
last_reset_time = now;
```

### 2. Reset Type Selection:

- Ctrl+Alt+Delete = Cold reset (clear everything)
- Ctrl+Alt+Backspace = Warm reset (keep some state)

```cpp
if (ctrl_pressed && alt_pressed && delete_pressed) {
    hd6301_trigger_reset_cold();
} else if (ctrl_pressed && alt_pressed && backspace_pressed) {
    hd6301_trigger_reset_warm();
}
```

### 3. Confirmation Dialog:

Show message: "Press again to confirm reset"

### 4. Reset Counter:

Track how many times reset has been triggered (useful for debugging).

---

## Summary

**XRESET via keyboard shortcut is the best solution:**

✅ **Easy to implement** - 40 lines of code
✅ **Practical** - No extra hardware needed  
✅ **User-friendly** - Just press Ctrl+Alt+Del
✅ **Authentic** - Performs real HD6301 hardware reset
✅ **Debugging tool** - Very useful for development

The HD6301 emulator resets exactly as if the XRESET pin was pulsed, giving you authentic behavior without any additional wiring!


