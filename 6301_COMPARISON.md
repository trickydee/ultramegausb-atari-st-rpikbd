# 6301 Emulator Comparison: Current vs STEEM SSE r1441

## Summary

Compared the 6301 emulator code in this project against the latest STEEM SSE (r1441) to identify potential fixes for joystick reliability issues.

## Key Findings

### 1. **MODULO 6 BUG EXISTS IN BOTH** ⚠️
**Location:** `ireg.c` dr2_getb() function

**STEEM SSE (line 262):**
```c
value=(mousek*2)%6;
```

**Our Code (line 209 - FIXED):**
```c
value = st_mouse_buttons() * 2;  // We already fixed this!
```

**Status:** ✅ **We have a better implementation than STEEM!**
- STEEM still has the modulo 6 bug that causes both buttons to return 0
- Our fix removes the modulo operation entirely

---

### 2. **Mouse/Joystick Port Selection Logic**

**STEEM SSE (ireg.c lines 377-378):**
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

**Difference:**
- STEEM uses a config flag `SSEConfig.Port0Joy` to decide
- Our code uses `st_mouse_enabled()` runtime check
- **Our code explicitly handles Joystick 1 when mouse is enabled** (line 269)
- STEEM doesn't show this explicit handling in the same location

**Analysis:** Our implementation is more explicit about handling both mouse + joystick 1 simultaneously.

---

### 3. **Fire Button Reading**

**STEEM SSE (ireg.c lines 255-258):**
```c
if(stick[0]&BIT_7)
  mousek|=BIT_1;
if(stick[1]&BIT_7)
  mousek|=BIT_0;
```

**Our Code:**
Fire buttons are read in `HidInput.cpp` and passed through `st_mouse_buttons()` which returns `mouse_state`.

**Difference:**
- STEEM reads fire buttons directly from `stick[]` array with bit 7 checks
- Our code abstracts this through the HidInput layer
- Both use bit 0 for JOY1 and bit 1 for JOY0

**Analysis:** Functionally equivalent, just different abstraction layers.

---

### 4. **Joystick Movement Handling**

**STEEM SSE (ireg.c lines 367-373):**
```c
joy0mvt=stick[0]&0xF; // eliminate fire info
joy1mvt=stick[1]&0xF;
if(joy0mvt||joy1mvt)
{
  value=0; // not always right (game mouse+joy?) but can't do better yet
  value|=joy0mvt|(joy1mvt<<4);
  value=~value;
}
```

**Our Code (ireg.c lines 266-273):**
```c
if (st_mouse_enabled()) {
  value = (value & (~0xF)) | (mouse_x_counter&3)|((mouse_y_counter&3)<<2);
  value = (value & ~0xf0) | (~st_joystick() & 0xf0);
}
else {
  value = ~st_joystick();
}
```

**Key Difference:**
- STEEM starts with `value=0` when joysticks have movement
- Our code preserves mouse data in lower nibble when mouse is enabled
- STEEM has a comment: "not always right (game mouse+joy?) but can't do better yet"

**Analysis:** This could be significant! STEEM's approach of setting `value=0` might cause issues with simultaneous mouse + joystick use.

---

### 5. **Real-time Joystick Updates**

**STEEM SSE has these calls:**
```c
if(NumJoysticks)
  JoyGetPoses(); // this could be heavy? we do it at 2 points in 6301
```

This is called in **two places**:
1. In `dr2_getb()` before reading fire buttons (line 252)
2. In `dr4_getb()` before reading directions (line 363)

**Our Code:**
We don't have equivalent real-time polling within the 6301 emulator. Joystick state is updated in the main loop via `HidInput::handle_joystick()`.

**Analysis:** STEEM updates joystick state **on-demand when the 6301 reads it**. This could provide better timing accuracy for games that poll rapidly.

---

## Recommendations

### HIGH PRIORITY:

1. **✅ Keep our modulo 6 fix** - We're ahead of STEEM here!

2. **⚠️ Consider adding on-demand joystick polling**
   - Some games may poll joysticks at specific times
   - Current implementation updates every 10ms in main loop
   - STEEM updates when 6301 actually reads the registers
   - This could explain timing-sensitive game issues

3. **⚠️ Review the `value=0` initialization in STEEM**
   - STEEM sets `value=0` when joysticks have movement
   - Our code is more careful about preserving mouse data
   - But STEEM's comment suggests this is a known limitation

### MEDIUM PRIORITY:

4. **Consider the mouse/joystick simultaneous handling**
   - Our explicit handling of joystick 1 when mouse enabled might be better
   - But worth testing both approaches

### LOW PRIORITY:

5. **Code structure differences**
   - STEEM has more conditional compilation (#ifndef SSE_LIBRETRO)
   - Our code is cleaner but less flexible for different platforms

---

## Conclusion

**Good News:** Our fire button fix (removing modulo 6) is actually **better than STEEM's current code**!

**Potential Issue:** The main difference that could affect game compatibility is the **timing of joystick updates**. STEEM polls joysticks on-demand when the 6301 reads them, while we update on a fixed 10ms schedule.

**Recommendation:** The intermittent joystick issues in some games might be due to:
1. ✅ **Timing** - Games polling faster than our 10ms update rate
2. ✅ **Polling location** - Games expecting immediate response when reading registers
3. ❓ **The `value=0` initialization** - Though STEEM has the same approach

**Next Steps:**
- Test if adding on-demand polling in `dr4_getb()` improves compatibility
- Consider calling `HidInput::handle_joystick()` from within the 6301 emulator when registers are read
- Profile the performance impact of more frequent joystick polling



