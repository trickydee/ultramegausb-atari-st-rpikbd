/*
 * Runtime USB/Bluetooth Toggle Implementation
 * Allows enabling/disabling USB and Bluetooth at runtime without rebuild
 */

#include "runtime_toggle.h"
#include <stdbool.h>
#include <stdio.h>

#ifdef ENABLE_BLUEPAD32
#include "bluepad32_init.h"
#endif

// Runtime state variables (global so they can be accessed from anywhere)
static bool g_usb_enabled = true;
#ifdef ENABLE_BLUEPAD32
static bool g_bt_enabled = true;  // Default to USB + Bluetooth mode (both enabled)
#endif

// USB runtime control
void usb_runtime_enable(void) {
    if (g_usb_enabled) {
        return;  // Already enabled
    }
    g_usb_enabled = true;
    printf("USB enabled at runtime\n");
}

void usb_runtime_disable(void) {
    if (!g_usb_enabled) {
        return;  // Already disabled
    }
    g_usb_enabled = false;
    printf("USB disabled at runtime (polling stopped, TinyUSB remains initialized)\n");
}

bool usb_runtime_is_enabled(void) {
    return g_usb_enabled;
}

#ifdef ENABLE_BLUEPAD32
// Bluetooth runtime control
void bt_runtime_enable(void) {
    if (g_bt_enabled) {
        return;  // Already enabled
    }
    // Re-initialize Bluetooth if it was deinitialized
    if (!bluepad32_is_enabled()) {
        printf("Re-initializing Bluetooth...\n");
        bluepad32_enable();
    }
    g_bt_enabled = true;
    printf("Bluetooth enabled at runtime\n");
}

void bt_runtime_disable(void) {
    if (!g_bt_enabled) {
        return;  // Already disabled
    }
    g_bt_enabled = false;
    // Note: We don't deinitialize Bluetooth here to avoid complexity
    // Just stop polling - can be re-enabled later
    printf("Bluetooth disabled at runtime (polling stopped)\n");
}

bool bt_runtime_is_enabled(void) {
    return g_bt_enabled && bluepad32_is_enabled();
}
#endif

