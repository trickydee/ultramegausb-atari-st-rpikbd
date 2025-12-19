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
// Maximum number of Bluetooth keyboards and mice
#define MAX_BT_KEYBOARDS 2
#define MAX_BT_MICE 2

// Forward declarations for Core 1 pause/resume functions
extern void core1_pause_for_bt_enumeration(void);
extern void core1_resume_after_bt_enumeration(void);

// Storage for Bluetooth gamepad data
typedef struct {
    uni_gamepad_t gamepad;
    bool connected;
    bool updated;  // Set to true when new data arrives
} bt_gamepad_storage_t;

// Storage for Bluetooth keyboard data
typedef struct {
    uni_keyboard_t keyboard;
    bool connected;
    bool updated;  // Set to true when new data arrives
} bt_keyboard_storage_t;

// Storage for Bluetooth mouse data
typedef struct {
    uni_mouse_t mouse;
    bool connected;
    bool updated;  // Set to true when new data arrives
} bt_mouse_storage_t;

static bt_gamepad_storage_t bt_gamepads[MAX_BT_GAMEPADS] = {0};
static bt_keyboard_storage_t bt_keyboards[MAX_BT_KEYBOARDS] = {0};
static bt_mouse_storage_t bt_mice[MAX_BT_MICE] = {0};

// Store device pointer to slot mapping for each device type
static uni_hid_device_t* gamepad_device_map[MAX_BT_GAMEPADS] = {0};
static uni_hid_device_t* keyboard_device_map[MAX_BT_KEYBOARDS] = {0};
static uni_hid_device_t* mouse_device_map[MAX_BT_MICE] = {0};

// Find the first available slot for a device type, or find existing slot if device already mapped
static int find_slot(uni_hid_device_t* d, uni_hid_device_t** device_map, int max_slots) {
    // First, check if device is already mapped
    for (int i = 0; i < max_slots; i++) {
        if (device_map[i] == d) {
            return i;
        }
    }
    // Find first free slot
    for (int i = 0; i < max_slots; i++) {
        if (device_map[i] == NULL) {
            device_map[i] = d;
            return i;
        }
    }
    return -1;  // No free slot
}

// Clear slot when device disconnects
static void clear_slot(uni_hid_device_t* d, uni_hid_device_t** device_map, int max_slots) {
    for (int i = 0; i < max_slots; i++) {
        if (device_map[i] == d) {
            device_map[i] = NULL;
            break;
        }
    }
}

// Get the storage slot for a device based on its controller class
static bt_gamepad_storage_t* get_gamepad_storage(uni_hid_device_t* d) {
    int idx = find_slot(d, gamepad_device_map, MAX_BT_GAMEPADS);
    if (idx >= 0) {
        return &bt_gamepads[idx];
    }
    return NULL;
}

static bt_keyboard_storage_t* get_keyboard_storage(uni_hid_device_t* d) {
    int idx = find_slot(d, keyboard_device_map, MAX_BT_KEYBOARDS);
    if (idx >= 0) {
        return &bt_keyboards[idx];
    }
    return NULL;
}

static bt_mouse_storage_t* get_mouse_storage(uni_hid_device_t* d) {
    int idx = find_slot(d, mouse_device_map, MAX_BT_MICE);
    if (idx >= 0) {
        return &bt_mice[idx];
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

    // Bluetooth keys are now managed manually via the RST button on the splash screen
    // Keys will persist across reboots (if using TLV/flash persistence)
    // For in-memory databases, keys don't persist anyway, so deletion is unnecessary
    // logi("Clearing stored Bluetooth keys...\n");
    // uni_bt_del_keys_unsafe();

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
    
    // Check if this might be a gamepad by COD or name
    // COD 0x0508 = Gamepad/Joystick
    // Pause Core 1 immediately when any gamepad is discovered to prevent freeze during GATT service discovery
    bool might_be_gamepad = (cod == 0x0508) ||  // Gamepad COD
                            (name != NULL && (strstr(name, "Stadia") != NULL || 
                                              strstr(name, "Xbox") != NULL ||
                                              strstr(name, "XBOX") != NULL));
    
    if (might_be_gamepad) {
        logi("[DIAG] Pausing Core 1 immediately for gamepad device discovery (COD=0x%04X, name='%s')\n", 
             cod, name ? name : "(null)");
        core1_pause_for_bt_enumeration();
    }
    
    // Accept all HID devices: gamepads, keyboards, and mice
    logi("  -> Accepting device (will attempt connection)\n");
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_device_connected(uni_hid_device_t* d) {
    logi("bluepad32_platform: device connected: %p\n", d);
    
    // Check if this is an Xbox or Stadia gamepad and ensure Core 1 is paused
    // We check vendor/product ID if available
    uint16_t vendor_id = uni_hid_device_get_vendor_id(d);
    bool is_xbox_stadia = (vendor_id == 0x045E) ||  // Microsoft (Xbox)
                          (vendor_id == 0x18D1);    // Google (Stadia)
    
    // Ensure Core 1 is paused (may have been paused earlier during discovery)
    // The freeze happens during GATT service discovery which occurs here
    if (is_xbox_stadia) {
        logi("[DIAG] Ensuring Core 1 is paused for Xbox/Stadia device connection\n");
        core1_pause_for_bt_enumeration();
    }
    
    // Device type will be determined in on_device_ready()
    // Storage will be allocated when device type is known
}

static void my_platform_on_device_disconnected(uni_hid_device_t* d) {
    logi("bluepad32_platform: device disconnected: %p\n", d);
    
    // Ensure Core 1 is resumed if device disconnects during enumeration
    // (safety check in case resume wasn't called)
    core1_resume_after_bt_enumeration();
    
    // Clear storage for all device types (device might have been any type)
    bt_gamepad_storage_t* gp_storage = get_gamepad_storage(d);
    if (gp_storage) {
        gp_storage->connected = false;
        gp_storage->updated = false;
        memset(&gp_storage->gamepad, 0, sizeof(gp_storage->gamepad));
        clear_slot(d, gamepad_device_map, MAX_BT_GAMEPADS);
        // Only notify unmount if it was actually a gamepad
        extern void bluepad32_notify_unmount(void);
        bluepad32_notify_unmount();
    }
    
    bt_keyboard_storage_t* kb_storage = get_keyboard_storage(d);
    if (kb_storage && kb_storage->connected) {
        kb_storage->connected = false;
        kb_storage->updated = false;
        memset(&kb_storage->keyboard, 0, sizeof(kb_storage->keyboard));
        clear_slot(d, keyboard_device_map, MAX_BT_KEYBOARDS);
        extern void bluepad32_notify_keyboard_unmount(void);
        bluepad32_notify_keyboard_unmount();
    }
    
    bt_mouse_storage_t* mouse_storage = get_mouse_storage(d);
    if (mouse_storage && mouse_storage->connected) {
        mouse_storage->connected = false;
        mouse_storage->updated = false;
        memset(&mouse_storage->mouse, 0, sizeof(mouse_storage->mouse));
        clear_slot(d, mouse_device_map, MAX_BT_MICE);
        extern void bluepad32_notify_mouse_unmount(void);
        bluepad32_notify_mouse_unmount();
    }
}

static uni_error_t my_platform_on_device_ready(uni_hid_device_t* d) {
    logi("bluepad32_platform: device ready: %p\n", d);
    
    // Determine device type and mark appropriate storage as connected
    // Only call bluepad32_notify_mount() for gamepads (not keyboards/mice)
    if (uni_hid_device_is_gamepad(d)) {
        // Check if this is an Xbox or Stadia gamepad (known to cause Core 1 freeze)
        uint16_t vendor_id = uni_hid_device_get_vendor_id(d);
        uint16_t product_id = uni_hid_device_get_product_id(d);
        bool is_xbox_stadia = (vendor_id == 0x045E) ||  // Microsoft (Xbox)
                              (vendor_id == 0x18D1 && product_id == 0x9400);  // Google (Stadia)
        
        // Gamepad - notify HidInput and mark as connected
        bt_gamepad_storage_t* storage = get_gamepad_storage(d);
        if (storage) {
            storage->connected = true;
            storage->updated = false;
        }
        extern void bluepad32_notify_mount(void);
        bluepad32_notify_mount();
        logi("bluepad32_platform: gamepad ready\n");
        
        // Resume Core 1 after a short delay to ensure enumeration completes
        // Note: Core 1 should already be paused from device_discovered/device_connected
        if (is_xbox_stadia) {
            logi("[DIAG] Waiting 10ms before resuming Core 1 (already paused from discovery)...\n");
            sleep_ms(10);  // 10ms delay - minimal pause
            logi("[DIAG] Resuming Core 1 after Xbox/Stadia enumeration\n");
            core1_resume_after_bt_enumeration();
        }
    } else if (uni_hid_device_is_keyboard(d)) {
        // Keyboard - mark as connected and notify UI
        bt_keyboard_storage_t* storage = get_keyboard_storage(d);
        if (storage) {
            storage->connected = true;
            storage->updated = false;
        }
        extern void bluepad32_notify_keyboard_mount(void);
        bluepad32_notify_keyboard_mount();
        logi("bluepad32_platform: keyboard ready\n");
    } else if (uni_hid_device_is_mouse(d)) {
        // Mouse - mark as connected and notify UI
        bt_mouse_storage_t* storage = get_mouse_storage(d);
        if (storage) {
            storage->connected = true;
            storage->updated = false;
        }
        extern void bluepad32_notify_mouse_mount(void);
        bluepad32_notify_mouse_mount();
        logi("bluepad32_platform: mouse ready\n");
    } else {
        logi("bluepad32_platform: unknown device type ready\n");
    }
    
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    switch (ctl->klass) {
        case UNI_CONTROLLER_CLASS_GAMEPAD: {
            bt_gamepad_storage_t* storage = get_gamepad_storage(d);
            if (storage && storage->connected) {  // Only update if already marked as connected
                // Copy gamepad data
                storage->gamepad = ctl->gamepad;
                storage->updated = true;
            }
            break;
        }
        
        case UNI_CONTROLLER_CLASS_KEYBOARD: {
            bt_keyboard_storage_t* storage = get_keyboard_storage(d);
            if (storage) {
                if (!storage->connected) {
                    // First keyboard data - mark as connected
                    storage->connected = true;
                    logi("bluepad32_platform: keyboard data received (first time)\n");
                }
                // Copy keyboard data
                storage->keyboard = ctl->keyboard;
                storage->updated = true;
                // Debug: log first few keyboard samples
                static uint32_t kb_sample_count = 0;
                if (kb_sample_count < 5) {
                    kb_sample_count++;
                    logi("bluepad32_platform: keyboard data - modifiers=0x%02X, keys[0]=0x%02X\n",
                         ctl->keyboard.modifiers, 
                         ctl->keyboard.pressed_keys[0]);
                }
            }
            break;
        }
        
        case UNI_CONTROLLER_CLASS_MOUSE: {
            bt_mouse_storage_t* storage = get_mouse_storage(d);
            if (storage) {
                if (!storage->connected) {
                    // First mouse data - mark as connected
                    storage->connected = true;
                    logi("bluepad32_platform: mouse data received (first time)\n");
                }
                // Copy mouse data
                storage->mouse = ctl->mouse;
                storage->updated = true;
                // Debug: log first few mouse samples
                static uint32_t mouse_sample_count = 0;
                if (mouse_sample_count < 5) {
                    mouse_sample_count++;
                    logi("bluepad32_platform: mouse data - dx=%d, dy=%d, buttons=0x%02X\n",
                         ctl->mouse.delta_x, ctl->mouse.delta_y, ctl->mouse.buttons);
                }
            }
            break;
        }
        
        default:
            // Ignore other controller types
            logi("bluepad32_platform: unknown controller class: %d\n", ctl->klass);
            break;
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

// Public API to get Bluetooth keyboard data
// Returns true if keyboard is connected and has data
bool bluepad32_get_keyboard(int idx, void* out_keyboard) {
    if (idx < 0 || idx >= MAX_BT_KEYBOARDS || !out_keyboard) {
        return false;
    }
    
    if (bt_keyboards[idx].connected && bt_keyboards[idx].updated) {
        // Copy the keyboard data (caller's struct must match uni_keyboard_t layout)
        uni_keyboard_t* kb = (uni_keyboard_t*)out_keyboard;
        *kb = bt_keyboards[idx].keyboard;
        bt_keyboards[idx].updated = false;  // Mark as read
        return true;
    }
    
    return false;
}

// Peek at keyboard data without marking as read (for shortcuts that need to check state)
bool bluepad32_peek_keyboard(int idx, void* out_keyboard) {
    if (idx < 0 || idx >= MAX_BT_KEYBOARDS || !out_keyboard) {
        return false;
    }
    
    if (bt_keyboards[idx].connected) {
        // Copy the keyboard data without clearing the updated flag
        uni_keyboard_t* kb = (uni_keyboard_t*)out_keyboard;
        *kb = bt_keyboards[idx].keyboard;
        return true;
    }
    
    return false;
}

// Public API to get Bluetooth mouse data
// Returns true if mouse is connected and has data
bool bluepad32_get_mouse(int idx, void* out_mouse) {
    if (idx < 0 || idx >= MAX_BT_MICE || !out_mouse) {
        return false;
    }
    
    if (bt_mice[idx].connected && bt_mice[idx].updated) {
        // Copy the mouse data (caller's struct must match uni_mouse_t layout)
        uni_mouse_t* ms = (uni_mouse_t*)out_mouse;
        *ms = bt_mice[idx].mouse;
        bt_mice[idx].updated = false;  // Mark as read
        return true;
    }
    
    return false;
}

// Get count of connected Bluetooth keyboards
int bluepad32_get_keyboard_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_BT_KEYBOARDS; i++) {
        if (bt_keyboards[i].connected) {
            count++;
        }
    }
    return count;
}

// Get count of connected Bluetooth mice
int bluepad32_get_mouse_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_BT_MICE; i++) {
        if (bt_mice[i].connected) {
            count++;
        }
    }
    return count;
}

// Delete all stored Bluetooth pairing keys
void bluepad32_delete_pairing_keys(void) {
    uni_bt_del_keys_unsafe();
}

