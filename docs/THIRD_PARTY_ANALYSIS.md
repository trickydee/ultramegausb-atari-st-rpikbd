# Third-Party Repository Analysis: rp2-atarist-rpikb

## Overview
Analysis of logronoid's implementation to identify optimizations for our USB/Bluetooth performance issues.

## Key Architectural Differences

### 1. **Core 1 Timing - CRITICAL DIFFERENCE** ⚠️

**Their approach:**
```c
while (true) {
    hd6301_tx_empty(1);
    hd6301_run_clocks(CYCLES_PER_LOOP);  // NO DELAY - tight loop!
}
```

**Our approach:**
```cpp
while (true) {
    // ... various delay mechanisms (sleep_until, busy_wait, etc)
    sleep_until(tm);  // or busy_wait_us(), or __asm__("nop") loop
}
```

**Impact:** Their Core 1 runs in a tight loop with ZERO delays. This means:
- Core 1 never blocks on timers
- Core 1 never yields to Core 0
- Maximum CPU time for emulation
- No timer interrupt dependencies

**Recommendation:** Consider removing ALL delays from Core 1 and run it in a pure tight loop. This would eliminate any potential blocking from Bluetooth/USB interrupts affecting Core 1 timing.

---

### 2. **USB Polling Frequency**

**Their approach:**
- `tuh_task()` called every **20ms** (`SERIAL_POLL_INTERVAL_US = 20000`)
- Mouse polling: **750μs** (0.75ms) for USB mode
- Original mouse: **2ms**

**Our approach:**
- `tuh_task()` called every **1ms**
- USB HID keyboard/mouse: **1ms** (recently changed from 2ms)
- Joysticks: **10ms**

**Analysis:** They poll USB much less frequently but still get good responsiveness. This suggests:
- USB stack can handle less frequent polling
- More frequent polling may actually cause contention
- Their mouse polling at 750μs is interesting - faster than our 1ms

**Recommendation:** Try increasing `tuh_task()` interval to 5-10ms and see if USB responsiveness improves. The frequent polling might be causing overhead.

---

### 3. **Bluetooth Polling**

**Their approach:**
```c
while (true) {
    btloop_tick();  // Mouse update every 1ms
    handle_rx();
    reset_sequence_cb();
    if (!btstack_paused) {
        async_context_poll(cyw43_arch_async_context());  // Continuous polling
    }
    tight_loop_contents();
}
```

**Our approach:**
- Bluetooth polling every **100ms**
- `bluepad32_poll()` called periodically

**Analysis:** They poll Bluetooth continuously in a tight loop, not on a timer. This ensures:
- No missed Bluetooth events
- Immediate processing of controller data
- But only when running in Bluetooth mode (not USB mode)

**Recommendation:** When in Bluetooth mode, consider polling more frequently (every 1-5ms) or in a tight loop.

---

### 4. **Clock Speed**

**Their approach:**
- **225MHz** (not 250MHz)
- Voltage: **1.20V**

**Our approach:**
- **250MHz**
- Voltage: Default (likely 1.25V)

**Analysis:** Lower clock speed might reduce power consumption and heat, potentially improving stability.

**Recommendation:** Test at 225MHz to see if it improves USB/Bluetooth stability.

---

### 5. **BTstack Configuration - Buffer Limits**

**Their `btstack_config.h`:**
```c
// Limit number of ACL/SCO Buffer to use by stack to avoid cyw43 shared bus overrun
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3

// Enable and configure HCI Controller to Host Flow Control
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3
```

**Our approach:**
- May not have these specific limits configured

**Analysis:** These limits prevent CYW43 shared bus overrun, which could cause USB/Bluetooth conflicts.

**Recommendation:** Add these buffer limits to our `btstack_config.h` to prevent bus contention.

---

### 6. **No Display Overhead**

**Their approach:**
- No OLED display
- No UI updates blocking main loop

**Our approach:**
- OLED display updates every 50ms (recently reduced from 10ms)
- I2C communication can block main loop

**Analysis:** Display updates add latency. Their implementation has zero display overhead.

**Recommendation:** Consider disabling OLED when USB/Bluetooth performance is critical, or reduce update frequency further.

---

### 7. **Separate USB/Bluetooth Modes**

**Their approach:**
- **Exclusive modes**: USB mode OR Bluetooth mode (not both)
- Selected at boot via configuration
- When switching modes, CYW43 is deinitialized

**Our approach:**
- **Simultaneous**: USB and Bluetooth running together
- This is the main difference causing our issues

**Analysis:** Running both simultaneously creates resource contention:
- CYW43 and USB share bus resources
- Both need regular polling
- Core 1 timing affected by both interrupt sources

**Recommendation:** 
- **Option A**: Make USB/Bluetooth mutually exclusive (like theirs)
- **Option B**: If simultaneous is required, ensure proper resource sharing:
  - Reduce USB polling when Bluetooth active
  - Reduce Bluetooth polling when USB active
  - Ensure Core 1 timing is completely independent

---

### 8. **No `is_busy` Checks**

**Their approach:**
- Don't check `tuh_hid_is_busy()` before reading data
- Read data directly from buffers

**Our approach:**
- Recently removed `is_busy` checks (but this didn't help)

**Analysis:** They don't have this check, suggesting it's not necessary if buffers are managed correctly.

---

### 9. **Mouse Update Timing**

**Their approach:**
- USB mode: Mouse polled every **750μs** (0.75ms)
- Bluetooth mode: Mouse polled every **1ms**
- Original mouse: **2ms**

**Our approach:**
- Mouse polled every **1ms** (recently changed from 2ms)

**Recommendation:** Try 750μs for USB mouse polling to match their timing.

---

### 10. **Core 1 Initialization**

**Their approach:**
```c
static void core1_entry() {
    flash_safe_execute_core_init();
    // ... init code ...
    while (true) {
        hd6301_tx_empty(1);
        hd6301_run_clocks(CYCLES_PER_LOOP);
        // NO DELAYS - pure tight loop
    }
}
```

**Our approach:**
- Various delay mechanisms (sleep_until, busy_wait, etc.)
- Timing-dependent on timer system

**Recommendation:** Remove ALL delays from Core 1 and run in pure tight loop. This would make Core 1 timing completely independent of Core 0's USB/Bluetooth operations.

---

## Recommended Changes (Priority Order)

### High Priority

1. **Remove Core 1 delays** - Run Core 1 in pure tight loop (no sleep_until, no busy_wait)
2. **Add BTstack buffer limits** - Prevent CYW43 bus overrun
3. **Reduce USB polling frequency** - Try 5-10ms instead of 1ms
4. **Make USB/Bluetooth mutually exclusive** - Or implement proper resource sharing

### Medium Priority

5. **Reduce clock to 225MHz** - May improve stability
6. **Increase mouse polling to 750μs** - Match their USB timing
7. **Disable OLED when performance critical** - Or reduce to 100ms+

### Low Priority

8. **Review BTstack feature flags** - Disable unused features
9. **Optimize main loop structure** - Reduce `get_absolute_time()` calls

---

## Code Snippets to Try

### Core 1 Tight Loop (No Delays)
```cpp
void __not_in_flash_func(core1_entry)() {
    setup_hd6301();
    hd6301_reset(1);
    
    unsigned long count = 0;
    while (true) {
        count += CYCLES_PER_LOOP;
        g_core1_cycle_count = count;
        hd6301_tx_empty(serial_send_buf_empty());
        hd6301_run_clocks(CYCLES_PER_LOOP);
        // NO DELAYS - pure tight loop
    }
}
```

### BTstack Buffer Limits (Add to btstack_config.h)
```c
// Limit buffers to avoid cyw43 shared bus overrun
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
```

### Reduced USB Polling
```cpp
// In main loop - increase from 1ms to 5-10ms
if (absolute_time_diff_us(usb_poll_ms, tm) >= 5000) {  // 5ms instead of 1ms
    usb_poll_ms = tm;
    tuh_task();
}
```

---

## Conclusion

The main differences are:
1. **Core 1 runs in pure tight loop** (no delays) - This is likely the biggest factor
2. **USB/Bluetooth are mutually exclusive** - No resource contention
3. **Lower USB polling frequency** - Less overhead
4. **BTstack buffer limits** - Prevent bus overrun
5. **No display overhead** - Zero UI blocking

The most impactful change would be removing delays from Core 1 and making it run in a pure tight loop, making it completely independent of Core 0's USB/Bluetooth operations.

