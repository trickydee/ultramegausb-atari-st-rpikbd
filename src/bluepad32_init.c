/*
 * Bluepad32 Initialization
 * Separated from main.cpp to avoid HID type conflicts between TinyUSB and btstack
 */

#ifdef ENABLE_BLUEPAD32

#include <pico/cyw43_arch.h>
#include <pico/async_context.h>
#include <pico/async_context_poll.h>
#include <pico/btstack_run_loop_async_context.h>
#include <uni.h>
#include "bluepad32_platform.h"

// Global async context for btstack (accessed from main.cpp)
async_context_poll_t* g_btstack_async_context = NULL;

// Initialize Bluepad32 and return the async context
async_context_poll_t* bluepad32_init(void) {
    static async_context_poll_t btstack_async_context;
    
    // Initialize async_context for btstack
    if (!async_context_poll_init_with_defaults(&btstack_async_context)) {
        return NULL;
    }

    // Initialize CYW43 (WiFi/Bluetooth chip on Pico W)
    if (cyw43_arch_init()) {
        async_context_deinit(&btstack_async_context.core);
        return NULL;
    }

    // Set custom platform before uni_init()
    uni_platform_set_custom(get_my_platform());

    // Initialize btstack run loop with async_context
    const btstack_run_loop_t* btstack_run_loop = 
        btstack_run_loop_async_context_get_instance(&btstack_async_context.core);
    btstack_run_loop_init(btstack_run_loop);

    // Initialize Bluepad32
    uni_init(0, NULL);

    g_btstack_async_context = &btstack_async_context;
    return &btstack_async_context;
}

// Poll btstack async_context (non-blocking)
void bluepad32_poll(void) {
    if (g_btstack_async_context) {
        async_context_poll(&g_btstack_async_context->core);
    }
}

#endif // ENABLE_BLUEPAD32

