# Core 1 (HD6301 Emulator) Performance Optimizations

## Changes Implemented

### Optimization 1: Reduced Sleep Granularity (4x Improvement) ⚡

**File:** `src/main.cpp`

**Before:**
```c
#define CYCLES_PER_LOOP 1000  // Sleep for 1ms between batches
```

**After:**
```c
#define CYCLES_PER_LOOP 250  // Sleep for 250μs between batches (4x improvement)
```

**Impact:**
- Core 1 wakes **4 times more frequently**
- Serial interrupts processed within 250μs instead of 1ms
- Multi-byte IKBD commands delivered 4x faster
- ROM firmware gets command parameters more promptly

**Why This Helps:**
When the ST sends a multi-byte command like `0x14 0x01` (interrogate joystick), the ROM needs to receive both bytes quickly. With 1ms sleep, the parameter byte could be delayed up to 1ms after the command byte, causing the ROM to timeout or get confused.

---

### Optimization 2: Accurate TX Status Reporting ✅

**File:** `src/main.cpp`, `core1_entry()`

**Before:**
```c
hd6301_tx_empty(1);  // Always tell 6301 "TX is empty"
```

**After:**
```c
hd6301_tx_empty(SerialPort::instance().send_buf_empty() ? 1 : 0);
```

**Impact:**
- 6301 ROM now knows real TX buffer status
- Prevents ROM from sending when TX buffer is full
- Better flow control
- More authentic to real hardware behavior

**Why This Matters:**
The ROM firmware checks the TDRE (Transmit Data Register Empty) flag before sending bytes. If we always report "empty" when the buffer is actually full, the ROM might send bytes that get dropped or cause conflicts.

---

### Optimization 3: Serial Overrun Error Monitoring 🔍

**File:** `src/main.cpp`, `handle_rx_from_st()`

**Added:**
```c
// Check for serial overrun errors
extern u_char iram[];  // TRCSR register array
if (iram[0x11] & 0x40) {  // ORFE bit (Overrun/Framing Error)
    static int overrun_count = 0;
    if ((++overrun_count % 100) == 1) {
        printf("WARNING: Serial overrun error detected! (count: %d)\n", overrun_count);
    }
}
```

**Impact:**
- Can now detect when bytes are being dropped
- Diagnostic tool for identifying remaining issues
- Reports errors without spamming (every 100th occurrence)

**What Overrun Means:**
An overrun occurs when a new byte arrives from the ST while the ROM hasn't read the previous byte yet from RDR. This indicates the ROM is too slow processing incoming data.

---

## Performance Comparison

### Timing Analysis:

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| **Core 1 wake frequency** | Every 1ms | Every 250μs | 4x |
| **Serial interrupt latency** | 0-1ms | 0-250μs | 4x |
| **Max command delay** | 2-3ms | 0.5-0.75ms | 4x |
| **TX status accuracy** | Always "empty" | Real status | Correct |
| **Error monitoring** | None | Overrun detection | Added |

### CPU Cycle Emulation:

**Before:** 1000 cycles → sleep 1ms  
**After:** 250 cycles → sleep 250μs  

**Both maintain the same emulation speed** (~1MHz), just broken into smaller batches.

---

## Expected Results

### What Should Improve:

1. **Joystick interrogation (0x14) reliability**
   - ROM gets parameters faster
   - Commands complete without timeout

2. **Mode switching (0x15-0x19) consistency**
   - Commands processed more promptly
   - State changes happen immediately

3. **Fire button monitoring (0x18) responsiveness**
   - Mode change takes effect faster
   - Games see correct data format sooner

4. **General command processing**
   - All multi-byte commands benefit
   - Lower latency throughout

### What Won't Change:

- Overall emulation speed (still ~1MHz)
- Timing accuracy (still maintains correct cycle counts)
- Compatibility (no behavior changes, just faster response)

---

## Potential Issues to Watch For

### 1. Increased Loop Overhead

More frequent wake/sleep cycles means slightly more overhead.

**Monitoring:**
- Watch for CPU usage increase
- Check if Core 1 is keeping up
- Verify timing accuracy isn't affected

### 2. Timing-Sensitive Games

Some games might expect exact timing.

**Testing:**
- Test games that worked before (regression)
- Test games that didn't work (improvement)
- Watch for new timing issues

### 3. Power Consumption

More frequent wake cycles = slightly higher power usage.

**Impact:** Negligible on RP2040 with USB power

---

## Further Optimizations (If Needed)

### Option A: Even Smaller Batches

If 250μs isn't enough:
```c
#define CYCLES_PER_LOOP 100  // 100μs sleep (10x original)
```

**Trade-off:** More overhead, but even lower latency

### Option B: Smart Sleep

Detect serial activity and adjust sleep:
```c
if (bytes_received > 0) {
    // Serial active - short sleep
    tm = delayed_by_us(tm, 50);
} else {
    // Idle - normal sleep
    tm = delayed_by_us(tm, CYCLES_PER_LOOP);
}
```

**Trade-off:** More complex, but optimal performance

### Option C: No Sleep During Commands

Busy-wait while commands are being processed:
```c
static int active_counter = 0;
if (bytes_received > 0) {
    active_counter = 10;  // Stay active for 10 iterations
}
if (active_counter > 0) {
    active_counter--;
    // Don't sleep - process immediately
} else {
    sleep_until(tm);
}
```

**Trade-off:** Highest performance, but uses more CPU

---

## Testing Recommendations

### Step 1: Flash and Test Basic Functionality

1. Flash new firmware
2. Test keyboard typing (should work same as before)
3. Test mouse movement (should work same as before)
4. Test joystick (should work better!)

### Step 2: Test Problematic Games

Test games that previously had intermittent issues:
- Games with joystick interrogation
- Games that switch IKBD modes
- Games with fire button monitoring

### Step 3: Check for Overrun Errors

If you see messages like:
```
WARNING: Serial overrun error detected! (count: 100)
```

This means the ROM is still too slow, and we need to:
- Further reduce CYCLES_PER_LOOP
- Or implement smart sleep / busy-wait

### Step 4: Regression Testing

Test games that previously worked to ensure no new issues.

---

## Technical Details

### Why 250 Cycles?

**HD6301 in Real ST:**
- Clock speed: ~1MHz
- 1000 cycles = 1ms of real time
- Serial byte arrival: every ~1.28ms at 7812 baud

**Batching Strategy:**
- **Too large (1000 cycles):** High latency, commands delayed
- **Too small (10 cycles):** Excessive overhead, poor efficiency
- **Just right (250 cycles):** Good balance of latency vs overhead

**250 cycles:**
- Represents 250μs of emulated time
- Serial bytes arrive every 1280μs
- Core 1 checks 5 times per byte arrival
- Plenty of opportunity to process interrupts

### Serial Interrupt Flow:

```
Byte arrives from ST → UART FIFO → Core 0 reads → sci_in() sets RDRF
                                                         ↓
Core 1 wakes up (within 250μs) → instr_exec() → Checks serial_int()
                                                         ↓
                                              RDRF set? → Yes! Trigger interrupt
                                                         ↓
                                              ROM interrupt handler runs
                                                         ↓
                                              ROM reads RDR register
                                                         ↓
                                              Command processed!
```

---

## Summary of All Optimizations

### Core 0 (Main Loop) - v3.4.0:
1. ✅ Serial RX in main loop (checked every ~20μs)
2. ✅ Read all available bytes (not just one)
3. ✅ UART FIFO enabled (32-byte buffer)
4. ✅ 270MHz default overclock

### Core 1 (6301 Emulator) - v3.4.0:
5. ✅ Reduced CYCLES_PER_LOOP to 250 (4x better response)
6. ✅ Accurate TX status reporting
7. ✅ Overrun error monitoring

### Combined Effect:

The serial communication path from ST to ROM is now:
- **500x faster polling** (Core 0: 10ms → 20μs)
- **4x faster interrupt response** (Core 1: 1ms → 250μs)
- **Better buffering** (32-byte UART FIFO)
- **80% more CPU power** (270MHz vs 150MHz)

**Total improvement:** Commands should be processed **much more reliably**!

---

**Version:** 3.4.0  
**Date:** October 15, 2025  
**Branch:** serial-optimizations  
**Status:** ✅ Implemented - ready for testing!




