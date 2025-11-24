/*
 * Bluepad32 Custom Platform Implementation for Atari ST IKBD Emulator
 * Based on bluepad32/examples/pico_w/src/my_platform.c
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <pico/cyw43_arch.h>
#include <pico/time.h>
#include <uni.h>

#include "sdkconfig.h"

// Sanity check
#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Pico W must use BLUEPAD32_PLATFORM_CUSTOM"
#endif

// Maximum number of Bluetooth gamepads we can track
#define MAX_BT_GAMEPADS 4

// Storage for Bluetooth gamepad data
typedef struct {
    uni_gamepad_t gamepad;
    bool connected;
    bool updated;  // Set to true when new data arrives
} bt_gamepad_storage_t;

static bt_gamepad_storage_t bt_gamepads[MAX_BT_GAMEPADS] = {0};

// Get the storage slot for a device
static bt_gamepad_storage_t* get_gamepad_storage(uni_hid_device_t* d) {
    int idx = uni_hid_device_get_idx_for_instance(d);
    if (idx >= 0 && idx < MAX_BT_GAMEPADS) {
        return &bt_gamepads[idx];
    }
    return NULL;
}

// Platform Overrides
static void my_platform_init(int argc, const char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    logi("bluepad32_platform: init()\n");
}

static void my_platform_on_init_complete(void) {
    logi("bluepad32_platform: on_init_complete()\n");

    // Clear stored BT keys on each boot (can be changed if you want to remember devices)
    logi("Clearing stored Bluetooth keys...\n");
    uni_bt_del_keys_unsafe();

    // Wait a bit for HCI to be ready (the "HCI not ready" messages suggest it needs time)
    logi("Waiting for HCI to be ready...\n");
    sleep_ms(2000);  // Give HCI 2 seconds to initialize

    // Start scanning and autoconnect to supported controllers
    logi("Starting Bluetooth scanning and autoconnect...\n");
    uni_bt_start_scanning_and_autoconnect_unsafe();
    logi("Bluetooth scanning started - waiting for devices...\n");
    logi("Put your controller in pairing mode now!\n");

    // Turn off LED once init is done
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

static uni_error_t my_platform_on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    // Log all discovered devices for debugging
    char addr_str[18];
    snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    
    logi("BT Device discovered: addr=%s, name='%s', COD=0x%04X, RSSI=%d\n",
         addr_str, name ? name : "(null)", cod, rssi);
    
    // Filter out keyboards and mice - we only want gamepads for now
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD) == UNI_BT_COD_MINOR_KEYBOARD) {
        logi("  -> Ignoring keyboard\n");
        return UNI_ERROR_IGNORE_DEVICE;
    }
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_MICE) == UNI_BT_COD_MINOR_MICE) {
        logi("  -> Ignoring mouse\n");
        return UNI_ERROR_IGNORE_DEVICE;
    }

    logi("  -> Accepting device (will attempt connection)\n");
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_device_connected(uni_hid_device_t* d) {
    logi("bluepad32_platform: device connected: %p\n", d);
    
    bt_gamepad_storage_t* storage = get_gamepad_storage(d);
    if (storage) {
        storage->connected = true;
        storage->updated = false;
        memset(&storage->gamepad, 0, sizeof(storage->gamepad));
    }
}

static void my_platform_on_device_disconnected(uni_hid_device_t* d) {
    logi("bluepad32_platform: device disconnected: %p\n", d);
    
    bt_gamepad_storage_t* storage = get_gamepad_storage(d);
    if (storage) {
        storage->connected = false;
        storage->updated = false;
        memset(&storage->gamepad, 0, sizeof(storage->gamepad));
    }
}

static uni_error_t my_platform_on_device_ready(uni_hid_device_t* d) {
    logi("bluepad32_platform: device ready: %p\n", d);
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) {
        return;  // Only handle gamepads for now
    }

    bt_gamepad_storage_t* storage = get_gamepad_storage(d);
    if (storage) {
        // Copy gamepad data
        storage->gamepad = ctl->gamepad;
        storage->updated = true;
    }
}

static const uni_property_t* my_platform_get_property(uni_property_idx_t idx) {
    ARG_UNUSED(idx);
    return NULL;
}

static void my_platform_on_oob_event(uni_platform_oob_event_t event, void* data) {
    ARG_UNUSED(data);
    
    switch (event) {
        case UNI_PLATFORM_OOB_BLUETOOTH_ENABLED:
            logi("bluepad32_platform: Bluetooth enabled: %d\n", (bool)(data));
            break;
        default:
            // Ignore other events
            break;
    }
}

// Entry Point
struct uni_platform* get_my_platform(void) {
    static struct uni_platform plat = {
        .name = "Atari ST IKBD Platform",
        .init = my_platform_init,
        .on_init_complete = my_platform_on_init_complete,
        .on_device_discovered = my_platform_on_device_discovered,
        .on_device_connected = my_platform_on_device_connected,
        .on_device_disconnected = my_platform_on_device_disconnected,
        .on_device_ready = my_platform_on_device_ready,
        .on_oob_event = my_platform_on_oob_event,
        .on_controller_data = my_platform_on_controller_data,
        .get_property = my_platform_get_property,
    };

    return &plat;
}

// Public API to get Bluetooth gamepad data
// Uses void* to avoid exposing uni_gamepad_t in header (prevents HID type conflicts)
bool bluepad32_get_gamepad(int idx, void* out_gamepad) {
    if (idx < 0 || idx >= MAX_BT_GAMEPADS || !out_gamepad) {
        return false;
    }
    
    if (bt_gamepads[idx].connected && bt_gamepads[idx].updated) {
        // Copy the gamepad data (caller's struct must match uni_gamepad_t layout)
        uni_gamepad_t* gp = (uni_gamepad_t*)out_gamepad;
        *gp = bt_gamepads[idx].gamepad;
        bt_gamepads[idx].updated = false;  // Mark as read
        return true;
    }
    
    return false;
}

int bluepad32_get_connected_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_BT_GAMEPADS; i++) {
        if (bt_gamepads[i].connected) {
            count++;
        }
    }
    return count;
}

