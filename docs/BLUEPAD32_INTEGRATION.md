# Bluepad32 Bluetooth Gamepad Integration

## Status: ✅ Complete Implementation

### Completed
1. ✅ Added Bluepad32 as git submodule
2. ✅ Created custom platform implementation (`src/bluepad32_platform.c`)
3. ✅ Created Bluepad32 to Atari ST converter (`src/bluepad32_atari.cpp`)
4. ✅ Wired Bluepad32 into `handle_joystick()` system
5. ✅ Added conditional compilation guards
6. ✅ **Build System Integration (CMakeLists.txt)** - Conditionally builds for Pico W
7. ✅ **Main Loop Integration** - Uses `async_context_poll()` for non-blocking btstack integration
8. ✅ **Initialization Code** - Added to `main.cpp` with proper error handling

### Implementation Details

#### Build System (CMakeLists.txt)
- Detects Pico W boards (`pico_w` or `pico2_w`)
- Conditionally adds Bluepad32 source files
- Links Bluepad32 and btstack libraries (`pico_cyw43_arch_none`, `pico_btstack_classic`, `pico_btstack_cyw43`, `bluepad32`)
- Adds include directories for Bluepad32 headers and btstack config

#### Main Loop Integration (Option B - Timer/Polling Approach)
**Solution**: Uses Pico SDK's `async_context_poll` for non-blocking integration.

- `async_context_poll_init_with_defaults()` creates a pollable async context
- `btstack_run_loop_async_context_get_instance()` integrates btstack with async_context
- `async_context_poll()` is called every main loop iteration (non-blocking)
- This keeps Core 1 dedicated to the 6301 emulator

#### Initialization Code (main.cpp)
```cpp
#ifdef ENABLE_BLUEPAD32
    // Initialize async_context for btstack
    static async_context_poll_t btstack_async_context;
    async_context_poll_init_with_defaults(&btstack_async_context);

    // Initialize CYW43 (WiFi/Bluetooth chip)
    cyw43_arch_init();

    // Set custom platform and initialize btstack run loop
    uni_platform_set_custom(get_my_platform());
    const btstack_run_loop_t* btstack_run_loop = 
        btstack_run_loop_async_context_get_instance(&btstack_async_context.core);
    btstack_run_loop_init(btstack_run_loop);

    // Initialize Bluepad32
    uni_init(0, NULL);
#endif

// In main loop:
#ifdef ENABLE_BLUEPAD32
    async_context_poll(&btstack_async_context.core);  // Non-blocking
#endif
```

## Files Created

1. `src/bluepad32_platform.c` - Custom platform implementation
2. `include/bluepad32_platform.h` - Public API header
3. `src/bluepad32_atari.cpp` - Gamepad to Atari converter
4. `docs/BLUEPAD32_INTEGRATION.md` - This file

## Files Modified

1. `src/HidInput.cpp` - Added `get_bluepad32_joystick()` and integration
2. `include/HidInput.h` - Added function declaration

## Testing

Once the build system and run loop integration are complete:
1. Flash to Pico W
2. Pair a Bluetooth gamepad (DualSense, DualShock 4, Switch Pro, etc.)
3. Test joystick input in Atari ST games
4. Verify it works alongside USB controllers

## References

- [Bluepad32 Documentation](https://bluepad32.readthedocs.io/)
- [Pico W Example](https://github.com/ricardoquesada/bluepad32/tree/main/examples/pico_w)
- [BTstack Multithreading Guide](https://github.com/bluekitchen/btstack/blob/master/port/esp32/README.md#multi-threading)

