# Final Analysis: 6301 Code Comparison and Joystick Issues

## Executive Summary

After comprehensive comparison of our 6301 emulator against STEEM SSE r1441, I found:

1. ✅ **Our modulo 6 fix is BETTER than STEEM** (they still have the bug!)
2. ⚠️ **Main functional difference: On-demand polling** (STEEM updates when registers are read)
3. ✅ **Most other differences are infrastructure/debugging code** (not functional)
4. ⚠️ **Mouse timing is more sophisticated in STEEM** (cycle-accurate updates)

---

## Does Polling Frequency Affect Both USB and GPIO Joysticks?

### **YES - Both Are Affected Equally**

**Why GPIO joysticks are affected:**
- GPIO reads are instant (`gpio_get()`)
- BUT we only call `HidInput::handle_joystick()` every 10ms
- If game polls 6301 registers between our updates, it gets stale GPIO data

**Why USB joysticks are affected:**
- TinyUSB polls USB devices every 1-8ms (device-dependent)
- BUT we only read from TinyUSB every 10ms in `handle_joystick()`
- Fresh USB data sits in TinyUSB buffers until we read it

**The Common Problem:**
```
Time:        0ms      10ms     16.6ms    20ms     33.2ms
            |        |        |         |        |
We update:  ●--------●--------+---------●--------+
Game polls:          ●--------●---------+---------●
            |        |        |         |        |
Data seen:  A        A        B         B        C

Problem: If button pressed at 11ms and released at 19ms,
         game polling at 16.6ms sees OLD state (A), not current (B)!
```

---

## Key Functional Differences Found

### 1. **On-Demand Joystick Polling** ⚠️ CRITICAL

**STEEM (in ireg.c):**
```c
// In dr2_getb() - line 251
if(NumJoysticks)
  JoyGetPoses();  // Update joystick state RIGHT NOW

// In dr4_getb() - line 362
if(NumJoysticks)
  JoyGetPoses();  // Update joystick state RIGHT NOW
```

**Our Code:**
```c
// In main loop only
HidInput::instance().handle_joystick();  // Every 10ms
```

**Impact:** HIGH - This is likely the cause of game-specific issues.

---

### 2. **Mouse Movement Timing** ⚠️ MODERATE

**STEEM (in dr4_getb()):**
```c
// Lines 321-354 - Complex cycle-accurate mouse timing
if(!(ddr4&0xF) && (ddr2&1) 
  && (Ikbd.MouseVblDeltaX || Ikbd.MouseVblDeltaY) )
{
  // Update mouse position based on CPU cycles
  while(Ikbd.MouseCyclesPerTickX && cpu.ncycles>=Ikbd.MouseNextTickX
    &&Ikbd.click_x<abs_quick(Ikbd.MouseVblDeltaX))
  {
    if(Ikbd.MouseVblDeltaX<0)
      mouse_x_counter=_rotl(mouse_x_counter,1);
    else
      mouse_x_counter=_rotr(mouse_x_counter,1);
    Ikbd.click_x++;
    Ikbd.MouseNextTickX+=Ikbd.MouseCyclesPerTickX;
  }
  // Similar for Y axis...
}
```

**Our Code:**
```c
mouse_tick(cpu.ncycles, &mouse_x_counter, &mouse_y_counter);
```

**Impact:** MODERATE - Mouse works well in our implementation, but STEEM's is more accurate for high-speed movement.

---

### 3. **Fire Button Encoding** ✅ WE'RE BETTER!

**STEEM (ireg.c line 262):**
```c
value=(mousek*2)%6;  // BUG: Returns 0 when both buttons pressed!
```

**Our Code (FIXED):**
```c
value = st_mouse_buttons() * 2;  // Correctly returns 6 for both buttons
```

**Impact:** HIGH - We fixed a bug STEEM still has!

---

### 4. **Port 0 Mouse/Joystick Selection** ⚠️ MINOR

**STEEM (ireg.c lines 377-378):**
```c
if(!SSEConfig.Port0Joy)
  value=(value&(~0xF))|(mouse_x_counter&3)|((mouse_y_counter&3)<<2);
```

**Our Code (ireg.c lines 266-273):**
```c
if (st_mouse_enabled()) {
  value = (value & (~0xF)) | (mouse_x_counter&3)|((mouse_y_counter&3)<<2);
  // Add joystick 1
  value = (value & ~0xf0) | (~st_joystick() & 0xf0);
}
else {
  value = ~st_joystick();
}
```

**Impact:** MINOR - Our code explicitly handles Joystick 1 when mouse is enabled, which is actually better.

---

### 5. **Infrastructure Differences** ✅ NOT FUNCTIONAL

STEEM has extensive additional code for:
- Conditional compilation (`#ifdef SSE_IKBD_6301_*`)
- Debug infrastructure (ASSERT, TRACE)
- Multiple emulation modes (LibRetro, etc.)
- Crash detection
- Performance counters

**Impact:** NONE - These don't affect core functionality.

---

## Root Cause Analysis

### **Why Some Games Work and Others Don't**

**Games That Work:**
1. Poll slowly (< 100 Hz / > 10ms intervals)
2. Use IKBD event reporting mode (6301 sends changes automatically)
3. Don't have strict timing requirements
4. Sample input during VBL when our 10ms updates are likely fresh

**Games That Don't Work:**
1. Poll rapidly (> 100 Hz / < 10ms intervals)
2. Poll at specific moments (e.g., mid-frame, multiple times per frame)
3. Expect immediate response when reading DR2/DR4
4. Have timing-sensitive mechanics (combo inputs, tap vs hold)
5. Test for button release immediately after press

**Examples of Timing-Sensitive Games:**
- Fast arcade ports (Space Harrier, R-Type, Xenon)
- Fighting games (combo inputs)
- Precision platformers (frame-perfect jumps)
- Rhythm games (timing-based input)

---

## Recommendations

### **HIGH PRIORITY - Implement On-Demand Polling**

Add joystick updates in `dr2_getb()` and `dr4_getb()`:

```c
// In 6301/ireg.c

static u_char dr2_getb (offs)
  u_int offs;
{
  u_char value;
  
  // ADD THIS: Update joystick state on-demand
  extern void update_joystick_state();
  update_joystick_state();
  
  value=0xFF;
  if(st_mouse_buttons()) {
    value = st_mouse_buttons() * 2;
  }
  return value;
}

static u_char dr4_getb (offs)
  u_int offs;
{
  u_char value;
  u_char  ddr2=iram[DDR2];
  u_char  ddr4=iram[DDR4];
  u_char  dr2=iram[P2];
  
  value=0xFF;
  mouse_tick(cpu.ncycles, &mouse_x_counter, &mouse_y_counter);
  
  if(!ddr4 && (ddr2&1) && (dr2&1))
  {
    // ADD THIS: Update joystick state on-demand
    extern void update_joystick_state();
    update_joystick_state();
    
    if (st_mouse_enabled()) {
      value = (value & (~0xF)) | (mouse_x_counter&3)|((mouse_y_counter&3)<<2);
      value = (value & ~0xf0) | (~st_joystick() & 0xf0);
    }
    else {
      value = ~st_joystick();
    }
    return value;
  }
}
```

**In HidInput.cpp, add:**
```cpp
extern "C" void update_joystick_state() {
    HidInput::instance().handle_joystick();
}
```

**Benefits:**
- ✅ Matches STEEM's behavior
- ✅ Games get fresh data when they poll
- ✅ Should fix timing-sensitive games
- ✅ Still maintains 10ms updates for efficiency (on-demand is additional)

**Drawbacks:**
- ❌ More CPU usage (polling on every register read)
- ❌ Could be called very frequently (performance impact)
- ❌ Need to measure actual impact

---

### **MEDIUM PRIORITY - Optimize Polling**

If on-demand polling is too expensive:

1. **Rate limit on-demand updates:**
```cpp
extern "C" void update_joystick_state() {
    static absolute_time_t last_update = 0;
    absolute_time_t now = get_absolute_time();
    
    // Only update if > 1ms since last update
    if (absolute_time_diff_us(last_update, now) > 1000) {
        HidInput::instance().handle_joystick();
        last_update = now;
    }
}
```

2. **Increase main loop frequency:**
   - Change from 10ms to 5ms or 2ms
   - Less accurate than on-demand but better than current
   - Lower CPU impact than full on-demand

---

### **LOW PRIORITY - Mouse Timing**

Consider implementing STEEM's cycle-accurate mouse timing if mouse movement feels imprecise at high speeds. Currently not a reported issue.

---

## Testing Plan

### **Phase 1: Measure Current Behavior**
1. Add counters to track how often DR2/DR4 are read
2. Measure time between reads for different games
3. Identify games that poll faster than 10ms

### **Phase 2: Implement On-Demand Polling**
1. Add `update_joystick_state()` function
2. Call from `dr2_getb()` and `dr4_getb()`
3. Test with problematic games

### **Phase 3: Optimize If Needed**
1. If performance impact is high, add rate limiting
2. Profile CPU usage
3. Find optimal balance between accuracy and performance

---

## Conclusion

**The Core Issue:**
- ✅ Our code is functionally correct (even better than STEEM for button encoding!)
- ⚠️ The timing model is different (fixed schedule vs on-demand)
- ⚠️ This affects BOTH USB and GPIO joysticks equally
- ⚠️ Games with rapid polling or specific timing expectations fail

**The Solution:**
Implement on-demand joystick polling in `dr2_getb()` and `dr4_getb()` to match STEEM's behavior and provide fresh data exactly when games need it.

**Expected Outcome:**
Games that currently have intermittent joystick issues should work reliably after implementing on-demand polling.

---

## Files Requiring Changes

1. **6301/ireg.c** - Add `update_joystick_state()` calls
2. **src/HidInput.cpp** - Add extern "C" wrapper function
3. **include/HidInput.h** - Declare extern "C" function

**Estimated Implementation Time:** 30 minutes
**Testing Time:** 1-2 hours with various games
**Risk Level:** LOW (can easily revert if issues arise)


