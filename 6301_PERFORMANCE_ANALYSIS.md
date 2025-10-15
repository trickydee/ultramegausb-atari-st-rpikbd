# HD6301 Emulator Performance Analysis

## Architecture Overview

### Dual-Core Design:

**Core 0 (Main Loop):**
- USB HID processing
- Serial RX from Atari ST
- Mouse/Keyboard/Joystick handling
- UI updates

**Core 1 (6301 Emulator):**
- HD6301 CPU emulation
- ROM firmware execution
- Serial interrupt processing
- Command handling

---

## Current Performance Issues Identified

### Issue 1: Core 1 Sleep Granularity ⚠️

**File:** `src/main.cpp`, `core1_entry()`

```c
#define CYCLES_PER_LOOP 1000

void core1_entry() {
    while (true) {
        hd6301_run_clocks(CYCLES_PER_LOOP);  // Run 1000 cycles
        tm = delayed_by_us(tm, CYCLES_PER_LOOP);
        sleep_until(tm);  // Sleep for 1000 microseconds (1ms)
    }
}
```

**Problem:**
- Emulates 1000 CPU cycles
- Then sleeps for 1ms (1000 microseconds)
- During sleep, **cannot process serial interrupts**
- At 7812 baud, byte arrives every ~1.28ms
- If byte arrives during sleep, ROM must wait up to 1ms to process it

**Impact:**
- Command processing latency: 0-1ms per byte
- Multi-byte commands: up to 2-3ms total delay
- Mode switching commands may be delayed
- ROM might timeout waiting for parameters

### Issue 2: Cycles Per Loop vs Interrupt Response

**Current:** 1000 cycles per batch

At ~1MHz emulated speed:
- 1000 cycles = 1ms of emulated time
- Matches real HD6301 timing perfectly
- But adds latency for interrupt processing

**Serial interrupt check happens:**
- In `instr_exec()` line 58: `else if (serial_int())`
- Only checked **between instructions**
- If running long instruction, interrupt waits

---

## Proposed Optimizations

### Optimization 1: Reduce Sleep Granularity 🎯

**Change CYCLES_PER_LOOP to smaller batches:**

```c
// Current: 1000 cycles = 1ms sleep
#define CYCLES_PER_LOOP 1000

// Proposed: 250 cycles = 250μs sleep
#define CYCLES_PER_LOOP 250
```

**Benefits:**
- ✅ Core 1 wakes 4x more frequently
- ✅ Serial interrupts processed within 250μs instead of 1ms
- ✅ Better command response time
- ✅ ROM gets parameters faster

**Drawbacks:**
- ⚠️ More loop overhead (sleep/wake cycles)
- ⚠️ Slightly higher CPU usage on Core 1
- ⚠️ Still maintains accurate timing (just smaller batches)

**Impact:** Should improve command processing by 4x

---

### Optimization 2: Variable Sleep Based on Serial Activity 🎯🎯

**Smart sleep - short sleep when serial active:**

```c
void core1_entry() {
    while (true) {
        hd6301_run_clocks(CYCLES_PER_LOOP);
        
        // Check if serial data is pending
        if (iram[TRCSR] & RDRF) {
            // Data waiting - use shorter sleep for faster processing
            tm = delayed_by_us(tm, 100);  // 100μs
        } else {
            // No serial activity - normal sleep
            tm = delayed_by_us(tm, CYCLES_PER_LOOP);
        }
        sleep_until(tm);
    }
}
```

**Benefits:**
- ✅ Fast response when commands arrive
- ✅ Low overhead when idle
- ✅ Best of both worlds

**Drawbacks:**
- ⚠️ Timing accuracy varies (might affect some games)
- ⚠️ More complex

---

### Optimization 3: Busy-Wait During Serial Activity 🎯🎯🎯

**Don't sleep at all when processing commands:**

```c
void core1_entry() {
    absolute_time_t tm = get_absolute_time();
    bool serial_active = false;
    int idle_count = 0;
    
    while (true) {
        hd6301_run_clocks(CYCLES_PER_LOOP);
        
        // Check for serial activity
        if (iram[TRCSR] & RDRF) {
            serial_active = true;
            idle_count = 0;
        } else if (serial_active) {
            idle_count++;
            if (idle_count > 10) {
                serial_active = false;  // No activity for 10 iterations
            }
        }
        
        if (!serial_active) {
            // Idle - use accurate timing with sleep
            tm = delayed_by_us(tm, CYCLES_PER_LOOP);
            sleep_until(tm);
        }
        // else: busy-wait (no sleep) for lowest latency
    }
}
```

**Benefits:**
- ✅ Zero latency during command processing
- ✅ Multi-byte commands processed immediately
- ✅ Still maintains timing accuracy when idle

**Drawbacks:**
- ⚠️ Higher CPU usage during commands
- ⚠️ More complex logic

---

### Optimization 4: Inter-Core Communication Signal 🔔

**Use multicore FIFO to wake Core 1:**

```c
// In handle_rx_from_st() (Core 0):
void handle_rx_from_st() {
    unsigned char data;
    while (SerialPort::instance().recv(data)) {
        if (!hd6301_sci_busy()) {
            hd6301_receive_byte(data);
            // Signal Core 1 that data arrived
            multicore_fifo_push_timeout_us(0x01, 0);
        } else {
            break;
        }
    }
}

// In core1_entry() (Core 1):
void core1_entry() {
    while (true) {
        hd6301_run_clocks(CYCLES_PER_LOOP);
        
        // Check for wake signal from Core 0
        uint32_t signal;
        if (multicore_fifo_pop_timeout_us(0, &signal)) {
            // Serial data arrived - don't sleep, process immediately
            continue;
        }
        
        tm = delayed_by_us(tm, CYCLES_PER_LOOP);
        sleep_until(tm);
    }
}
```

**Benefits:**
- ✅ Core 1 wakes immediately when serial data arrives
- ✅ No polling overhead
- ✅ Hardware-supported signaling

**Drawbacks:**
- ⚠️ Adds inter-core communication overhead
- ⚠️ FIFO might fill up if Core 1 is slow

---

### Optimization 5: Increase 6301 Emulation Speed 🚀

**Run emulator faster than real hardware:**

The HD6301 in the real ST runs at ~1MHz. But we're emulating it - we could run it **faster**!

```c
// Current: 1000 cycles per 1000μs = 1MHz (authentic)
// Proposed: 1000 cycles per 500μs = 2MHz (2x speed)
// Or even:  1000 cycles per 250μs = 4MHz (4x speed)

void core1_entry() {
    while (true) {
        hd6301_run_clocks(CYCLES_PER_LOOP);
        tm = delayed_by_us(tm, CYCLES_PER_LOOP / 2);  // Run 2x speed
        sleep_until(tm);
    }
}
```

**Benefits:**
- ✅ Commands processed 2-4x faster
- ✅ Lower latency for everything
- ✅ Simple change

**Drawbacks:**
- ⚠️ Not authentic to real hardware timing
- ⚠️ Might break timing-sensitive games
- ⚠️ Serial baud rate remains 7812 (ST side), only processing is faster

**Risk Level:** MEDIUM - needs testing

---

### Optimization 6: Remove Unnecessary TX Status Updates

**File:** `src/main.cpp`, `core1_entry()`

```c
while (true) {
    // This is called EVERY iteration
    hd6301_tx_empty(1);  // Always tells 6301 "TX is empty"
    
    hd6301_run_clocks(CYCLES_PER_LOOP);
}
```

**Analysis:**
Currently, we tell the 6301 that TX is always empty. This is wasteful if TX status rarely changes.

**Proposed:**
```c
// Only update TX status when it actually changes
static bool last_tx_state = false;
bool tx_empty = SerialPort::instance().send_buf_empty();
if (tx_empty != last_tx_state) {
    hd6301_tx_empty(tx_empty);
    last_tx_state = tx_empty;
}
```

**Impact:** Minimal (but cleaner code)

---

## Recommended Optimizations (Priority Order)

### 1. ⭐⭐⭐ Reduce CYCLES_PER_LOOP (Quick Win)

**Change from 1000 to 250:**
- Simple one-line change
- 4x better interrupt response
- Low risk
- Maintains timing accuracy

### 2. ⭐⭐ Busy-Wait During Serial Activity

**Don't sleep when processing commands:**
- More complex but effective
- Zero latency during command bursts
- Still accurate timing when idle

### 3. ⭐ Run Emulator at 2x Speed

**Double the 6301 emulation speed:**
- Commands process 2x faster
- Needs thorough testing
- Might affect game compatibility

---

## Testing Strategy

### Test Each Optimization Separately:

1. **Baseline:** Current code (1000 cycles, 1ms sleep)
2. **Test 1:** 250 cycles, 250μs sleep
3. **Test 2:** Smart busy-wait during serial
4. **Test 3:** 2x emulation speed
5. **Test 4:** Combination of 1+2

### Metrics to Measure:

1. **Command response time:** How fast does mode switching happen?
2. **Game compatibility:** Do games work correctly?
3. **Timing accuracy:** Are VBL-synced operations still accurate?
4. **CPU usage:** Is Core 1 maxed out?

### Games to Test:

- Games with joystick interrogation (0x14)
- Games with mode switching (0x15-0x19)
- Games with timing-sensitive input
- Games that worked before (regression test)

---

## Implementation Plan

### Phase 1: Low-Risk Quick Win ✅

```c
// In main.cpp
#define CYCLES_PER_LOOP 250  // Changed from 1000
```

**Test this first** - if it solves the problem, stop here!

### Phase 2: Smart Sleep (If Phase 1 Not Enough)

Add logic to detect serial activity and adjust sleep accordingly.

### Phase 3: Speed Multiplier (If Desperate)

Run emulator faster than real hardware (higher risk).

---

## Code Changes for Phase 1

### File: `src/main.cpp`

**Before:**
```c
#define CYCLES_PER_LOOP 1000
```

**After:**
```c
#define CYCLES_PER_LOOP 250  // Reduced for better interrupt response
```

That's it! Just one line!

---

## Additional Issues Found

### Issue A: TX Empty Always Called

In `core1_entry()`:
```c
hd6301_tx_empty(1);  // Called every iteration, always 1
```

This tells the 6301 "TX is always empty" which might not be true if the SerialPort TX buffer is full.

**Better:**
```c
hd6301_tx_empty(SerialPort::instance().send_buf_empty() ? 1 : 0);
```

### Issue B: No Overrun Error Reporting

When `sci_in()` detects overrun (RDRF already set), it sets the ORFE flag but we never check it in our C++ code.

**Add monitoring:**
```c
// In handle_rx_from_st():
if (iram[TRCSR] & ORFE) {
    printf("WARNING: Serial overrun detected!\n");
}
```

This would tell us if bytes are being dropped.

---

## Summary

### Critical Finding:

The **1ms sleep in Core 1** is likely the bottleneck causing intermittent issues with IKBD commands.

### Recommended Fix:

**Change one line:**
```c
#define CYCLES_PER_LOOP 250  // Down from 1000
```

This gives Core 1 four times more opportunities to check for serial interrupts per second, reducing command processing latency from 0-1ms to 0-250μs.

### If That Doesn't Work:

Try the smart busy-wait approach or increase emulation speed, but those have higher risk of breaking timing-sensitive code.

---

**Priority:** HIGH  
**Risk:** LOW (for Phase 1)  
**Effort:** 1 minute (change one number)  
**Expected Impact:** 4x improvement in command processing latency

