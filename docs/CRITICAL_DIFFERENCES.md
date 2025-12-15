# Critical Differences Between Our Code and Logronoid's

## Issues Found After Core 1 Tight Loop Implementation

### 1. **CYCLES_PER_LOOP - CRITICAL TIMING ISSUE** ⚠️

**Their value:** `CYCLES_PER_LOOP = 1000`
**Our value:** `CYCLES_PER_LOOP = 100`

**Impact:** 
- We're running 10x fewer cycles per loop iteration
- With no delays, Core 1 runs much faster than intended
- The 6301 emulator needs to run at approximately 1MHz
- Running too fast causes serial timing issues and application lockups

**Fix:** Change `CYCLES_PER_LOOP` from 100 to 1000 to match their implementation

---

### 2. **Clock Speed**

**Their value:** 225MHz
**Our value:** 250MHz

**Impact:**
- Higher clock speed may cause stability issues
- May contribute to USB/Bluetooth conflicts

**Fix:** Consider reducing to 225MHz to match their stable configuration

---

### 3. **USB Polling Frequency**

**Their value:** `tuh_task()` every 20ms
**Our value:** `tuh_task()` every 1ms

**Impact:**
- Too frequent polling may cause USB stack overhead
- May contribute to USB/Bluetooth conflicts

**Fix:** Increase USB polling interval to 5-10ms (compromise between their 20ms and our 1ms)

---

### 4. **USB/Bluetooth Mutually Exclusive**

**Their approach:** USB mode OR Bluetooth mode (selected at boot)
**Our approach:** USB AND Bluetooth simultaneously

**Impact:**
- Running both simultaneously causes resource contention
- CYW43 and USB share bus resources
- This is the root cause of "Bluetooth breaks USB" issue

**Fix Options:**
- **Option A:** Make USB/Bluetooth mutually exclusive (like theirs)
- **Option B:** Implement proper resource sharing with reduced polling when both active

---

### 5. **Serial Timing with Pure Tight Loop**

**Problem:** 
- With `CYCLES_PER_LOOP = 100` and no delays, Core 1 runs too fast
- The 6301 emulator needs proper timing to match real hardware (~1MHz)
- Applications that rely on precise timing will lock up

**Solution:**
- Increase `CYCLES_PER_LOOP` to 1000 (match logronoid)
- This maintains proper emulation speed even without delays
- The pure tight loop still works, but with correct cycle count

---

## Recommended Changes (Priority Order)

### High Priority (Fix Serial Timing)

1. **Change CYCLES_PER_LOOP from 100 to 1000**
   - This fixes the serial timing issues
   - Maintains proper 6301 emulation speed
   - Matches logronoid's proven configuration

### Medium Priority (Fix USB/Bluetooth Conflicts)

2. **Reduce USB polling frequency to 5-10ms**
   - Reduces USB stack overhead
   - May help with Bluetooth coexistence

3. **Consider reducing clock to 225MHz**
   - May improve stability
   - Matches logronoid's configuration

### Low Priority (Architectural)

4. **Make USB/Bluetooth mutually exclusive**
   - Would completely eliminate conflicts
   - Requires mode selection at boot
   - More complex user experience

---

## Implementation Notes

The pure tight loop approach is correct, but we need to:
- Use the correct `CYCLES_PER_LOOP` value (1000, not 100)
- This ensures proper emulation timing even without delays
- The tight loop still provides timer independence

