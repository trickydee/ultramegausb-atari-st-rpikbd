# Hybrid RAM/XIP Implementation Plan

## Goal
Keep all original code (6301 emulator, ROM image, USB processing) in RAM while keeping only the new Bluetooth components in XIP (flash). This is the **REVERSED** approach - simpler and better!

## Current Status
- **ROM Image**: Copied to RAM at startup (via `memcpy` in `setup_hd6301()`)
- **6301 Emulator**: Runs from flash (XIP) - **NEEDS TO MOVE TO RAM**
- **USB Processing**: Runs from flash (XIP) - **NEEDS TO MOVE TO RAM**
- **Bluetooth**: Runs from flash (XIP) - **KEEP IN XIP** (this is the new code)

## Approach: Use `copy_to_ram` + Force Bluetooth to XIP

**Strategy:**
1. Change binary type back to `copy_to_ram` (original working configuration)
2. Mark Bluepad32/btstack functions to stay in flash using a special attribute
3. All original code automatically goes to RAM (via `copy_to_ram`)
4. Only Bluetooth code stays in XIP

**However**, there's a limitation: `copy_to_ram` copies the entire binary to RAM. We can't selectively keep some code in flash when using `copy_to_ram`.

## Better Approach: Use `default` (XIP) + Force Original Code to RAM ✅ IMPLEMENTED

**Strategy:**
1. Keep binary type as `default` (XIP) for Bluetooth builds
2. Mark all original code functions with `__not_in_flash_func` to force them into RAM
3. Leave Bluepad32/btstack functions unmarked (they stay in XIP automatically)
4. This gives us the hybrid we want: original code in RAM, Bluetooth in XIP

## Implementation Status

### ✅ Completed:
- **6301 Emulator Functions:**
  - `hd6301_init()`, `hd6301_destroy()`, `hd6301_reset()`
  - `hd6301_trigger_reset()`, `hd6301_run_clocks()`
  - `hd6301_receive_byte()`, `hd6301_tx_empty()`, `hd6301_sci_busy()`
  
- **Serial Communication (sci.c):**
  - `sci_in()`, `trcsr_getb()`, `trcsr_putb()`
  - `rdr_getb()`, `tdr_putb()`
  
- **Instruction Execution:**
  - `instr_exec()` - Core instruction execution loop
  
- **Main Loop Functions:**
  - `handle_rx_from_st()` - Serial receive handler
  - `setup_hd6301()` - Initialization
  - `core1_entry()` - Core 1 entry point
  
- **Serial Port (SerialPort.cpp):**
  - `SerialPort::send()`, `SerialPort::recv()`
  - `serial_send()` - C wrapper

### ⚠️ Note on Memory Functions:
- `mem_getb()` and `mem_putb()` are static inline functions in `memory.h`
- They will be inlined into the marked functions that call them
- No explicit marking needed (inlined into RAM functions)

### ⚠️ Note on USB Processing:
- USB callback functions (`tuh_hid_*`) are called from TinyUSB library
- These may need marking if performance issues persist
- Can be added incrementally if needed

## Implementation Strategy

### Option 1: Function-Level Marking (Recommended)
Use `__not_in_flash_func` attribute on critical functions. This is the cleanest approach and allows fine-grained control.

**Pros:**
- Selective - only mark what needs to be fast
- No linker script changes needed
- Easy to test incrementally

**Cons:**
- Requires marking many functions
- Need to identify all critical paths

### Option 2: Source File Compilation Flags
Use CMake to compile specific source files with flags that place them in RAM sections.

**Pros:**
- Entire files moved to RAM automatically
- Less code changes needed

**Cons:**
- Less granular control
- May move unnecessary code to RAM

### Option 3: Custom Linker Script
Create a custom linker script that places specific sections in RAM.

**Pros:**
- Most control over memory layout
- Can optimize placement

**Cons:**
- Complex to maintain
- Requires deep linker knowledge

## Recommended Approach: Option 1 (Function-Level)

### Step 1: Mark 6301 Emulator Functions

Files to modify in `6301/`:
- `6301.c` - Main emulator functions
- `cpu.c` - CPU core
- `instr.c` - Instruction execution
- `memory.c` - Memory access
- `sci.c` - Serial communication
- `ireg.c` - Internal registers
- `opfunc.c` - Operand functions
- `timer.c` - Timer functions

**Key functions to mark:**
```c
__not_in_flash_func(hd6301_run_clocks)
__not_in_flash_func(hd6301_receive_byte)
__not_in_flash_func(hd6301_send_byte)
__not_in_flash_func(sci_in)
__not_in_flash_func(sci_out)
__not_in_flash_func(mem_getb)
__not_in_flash_func(mem_putb)
__not_in_flash_func(instr_exec)
```

### Step 2: Mark USB Processing Functions

Files to modify in `src/`:
- `HidInput.cpp` - Main USB input handling
- `hid_app_host.c` - USB HID callbacks
- `xinput_atari.cpp` - Xbox controller processing
- `gamecube_adapter.c` - GameCube processing
- `switch_controller.c` - Switch controller
- `ps3_controller.c` - PS3 controller
- `ps4_controller.c` - PS4 controller
- `stadia_controller.c` - Stadia controller
- `SerialPort.cpp` - Serial communication

**Key functions to mark:**
```c
__not_in_flash_func(handle_keyboard)
__not_in_flash_func(handle_mouse)
__not_in_flash_func(handle_joystick)
__not_in_flash_func(tuh_hid_mounted_cb)
__not_in_flash_func(tuh_hid_report_received_cb)
__not_in_flash_func(SerialPort::send)
__not_in_flash_func(SerialPort::recv)
```

### Step 3: Mark Main Loop Functions

In `src/main.cpp`:
```c
__not_in_flash_func(handle_rx_from_st)
__not_in_flash_func(core1_entry)
__not_in_flash_func(setup_hd6301)
```

### Step 4: Move ROM Image to RAM (Optional Optimization)

Instead of copying ROM at startup, place it directly in RAM:
```c
// In src/HD6301V1ST.cpp
__not_in_flash("6301_rom") unsigned char rom_HD6301V1ST_img[] = {
    // ... ROM data ...
};
```

This eliminates the `memcpy` at startup, but uses more RAM.

### Step 5: Keep Bluetooth in XIP

**DO NOT** mark any Bluepad32 or btstack functions with `__not_in_flash_func`. They should remain in flash to save RAM.

## Memory Usage Estimate

**RAM Requirements:**
- 6301 emulator code: ~50-80 KB
- ROM image: 4 KB (already in RAM)
- USB processing code: ~30-50 KB
- **Total additional RAM: ~80-130 KB**

**Available RAM on Pico 2 W:**
- Total: 512 KB
- Current usage: ~200-250 KB (with XIP)
- After changes: ~280-380 KB
- **Remaining: ~130-230 KB** (should be sufficient)

## Testing Plan

1. **Incremental Testing:**
   - Start with 6301 emulator only
   - Test serial communication (ST ↔ Pico)
   - Add USB processing
   - Verify Bluetooth still works

2. **Performance Verification:**
   - Measure serial latency
   - Test with Dragonnels demo
   - Verify USB device responsiveness

3. **Memory Verification:**
   - Check RAM usage with `pico_get_unique_board_id()` or linker map
   - Ensure no RAM overflow

## Implementation Order

1. ✅ Document plan (this file)
2. Mark 6301 emulator functions
3. Mark USB processing functions
4. Mark main loop functions
5. Test serial communication
6. Test USB devices
7. Test Bluetooth (verify still works)
8. Optimize ROM placement (optional)

## Notes

- `__not_in_flash_func` is a macro that expands to `__attribute__((section(".time_critical.<function_name>")))`
- Functions marked this way are placed in RAM by the linker
- The linker automatically handles section placement
- No CMake changes needed for function-level marking
- Bluetooth code should remain untouched (stays in XIP)

## References

- [Pico SDK Platform Sections](https://github.com/raspberrypi/pico-sdk/blob/master/src/common/pico_platform_sections/include/pico/platform/sections.h)
- [Pico SDK Time Critical Functions](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#time-critical-functions)

