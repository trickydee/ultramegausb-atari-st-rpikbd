# Detailed 6301 Code Comparison: Our Project vs STEEM SSE r1441

## File Size Comparison

| File | Our Lines | STEEM Lines | Difference | Status |
|------|-----------|-------------|------------|--------|
| 6301.c | 178 | 427 | +249 | ⚠️ Major |
| ireg.c | 321 | 426 | +105 | ⚠️ Major |
| sci.c | 180 | 306 | +126 | ⚠️ Major |
| instr.c | 100 | 188 | +88 | ⚠️ Major |
| memory.c | 121 | 163 | +42 | ⚠️ Moderate |
| cpu.c | 23 | 34 | +11 | ⚠️ Minor |
| timer.c | 99 | 109 | +10 | ⚠️ Minor |
| opfunc.c | 1193 | 1216 | +23 | ✅ Minor |
| alu.c | 331 | 331 | 0 | ✅ Same |
| optab.c | 308 | 308 | 0 | ✅ Same |
| reg.c | 160 | 164 | +4 | ✅ Same |
| callstac.c | 140 | 140 | 0 | ✅ Same |
| symtab.c | 211 | 211 | 0 | ✅ Same |
| tty.c | 132 | 132 | 0 | ✅ Same |
| fprinthe.c | 80 | 80 | 0 | ✅ Same |
| memsetl.c | 18 | 18 | 0 | ✅ Same |

## Critical Findings

### 1. **ireg.c - Most Important for Joysticks!**

**Size:** 321 lines (ours) vs 426 lines (STEEM) = **105 lines missing**

This is the file that handles DR2/DR4 register reads (joystick/mouse input).

#### Key Differences Found:

**A. On-Demand Joystick Polling (ALREADY DISCUSSED)**
```c
// STEEM has this in dr2_getb() and dr4_getb():
if(NumJoysticks)
  JoyGetPoses();  // Update RIGHT NOW
```

**B. Mouse Movement Timing**
STEEM has complex mouse timing code in dr4_getb() (lines 321-354):
```c
if(!(ddr4&0xF) && (ddr2&1) 
  && (Ikbd.MouseVblDeltaX || Ikbd.MouseVblDeltaY) )
{
  // Update mouse at each read for stability
  while(Ikbd.MouseCyclesPerTickX && cpu.ncycles>=Ikbd.MouseNextTickX
    &&Ikbd.click_x<abs_quick(Ikbd.MouseVblDeltaX))
  {
    if(Ikbd.MouseVblDeltaX<0) // left
      mouse_x_counter=_rotl(mouse_x_counter,1);
    else  // right
      mouse_x_counter=_rotr(mouse_x_counter,1);
    Ikbd.click_x++;
    Ikbd.MouseNextTickX+=Ikbd.MouseCyclesPerTickX;
  }
  // Similar for Y axis...
}
```

**Our code:**
```c
mouse_tick(cpu.ncycles, &mouse_x_counter, &mouse_y_counter);
```

**Analysis:** STEEM updates mouse position **on-demand** when DR4 is read, calculating exact timing based on CPU cycles. Our code delegates to `mouse_tick()` which is called from the main loop.

**C. Configuration Checks**
STEEM has conditional compilation with `#ifndef SSE_LIBRETRO` and checks like `SSEConfig.Port0Joy`.

**Our code:** Simpler, no conditional compilation for different platforms.

---

### 2. **6301.c - Initialization and Interface**

**Size:** 178 lines (ours) vs 427 lines (STEEM) = **249 lines missing**

#### Key Differences:

**A. STEEM has extensive debug infrastructure:**
- Conditional ASSERT macros
- TRACE facilities
- Debug counters
- Crash detection

**B. STEEM includes additional files we don't:**
```c
#include "timer.c"
#include "optab.c"
#include "callstac.c"
#include "sci.c"
```

**Our code only includes:**
```c
#include "cpu.c"
#include "fprinthe.c"
#include "memsetl.c"
#include "symtab.c"
#include "memory.c"
#include "opfunc.c"
#include "reg.c"
#include "instr.c"
#include "ireg.c"
#include "timer.c"
#include "sci.c"
#include "optab.c"
```

**Analysis:** Mostly infrastructure differences, not functional.

---

### 3. **sci.c - Serial Communication Interface**

**Size:** 180 lines (ours) vs 306 lines (STEEM) = **126 lines missing**

This file handles serial communication between the 6301 and the Atari ST's ACIA.

Let me check the differences...




