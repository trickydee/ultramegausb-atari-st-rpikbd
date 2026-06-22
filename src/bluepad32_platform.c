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
#include "config.h"
#include "version.h"

#if ENABLE_SERIAL_LOGGING
#define DIAG_LOGI(...) logi(__VA_ARGS__)
#else
#define DIAG_LOGI(...) ((void)0)
#endif

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
extern void core1_wait_for_pause_active(uint32_t timeout_ms);
extern uint32_t core1_get_pause_depth(void);

// busy_wait_us is safe inside BT callbacks; sleep_ms/__wfe are not (timer/IRQ may stall).
static void bt_callback_busy_wait_ms(uint32_t ms) {
    busy_wait_us(ms * 1000u);
}

// Throttled BT input diagnostics (serial debug)
static uint32_t bt_diag_kb_reports = 0;
static uint32_t bt_diag_mouse_reports = 0;
static uint32_t bt_diag_joy_reports = 0;
static uint32_t bt_diag_kb_cb_drop = 0;
static uint32_t bt_diag_mouse_cb_drop = 0;
static uint32_t bt_diag_joy_cb_drop = 0;
static uint32_t bt_diag_joy_cb_early = 0;
static uint32_t bt_diag_kb_get_ok = 0;
static uint32_t bt_diag_kb_get_noupd = 0;
static uint32_t bt_diag_mouse_get_ok = 0;
static uint32_t bt_diag_mouse_get_noupd = 0;
static uint32_t bt_diag_joy_get_ok = 0;
static uint32_t bt_diag_joy_get_noupd = 0;
static absolute_time_t bt_diag_last_log = {0};
static absolute_time_t bt_diag_last_snapshot = {0};

static void bt_diag_maybe_log(void) {
    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(bt_diag_last_log, now) < 5000000) {
        return;
    }
    bt_diag_last_log = now;
    DIAG_LOGI("[DIAG] BT reports/5s: kb=%lu mouse=%lu joy=%lu pause_depth=%lu\n",
         (unsigned long)bt_diag_kb_reports,
         (unsigned long)bt_diag_mouse_reports,
         (unsigned long)bt_diag_joy_reports,
         (unsigned long)core1_get_pause_depth());
    bt_diag_kb_reports = 0;
    bt_diag_mouse_reports = 0;
    bt_diag_joy_reports = 0;
}

// Storage for Bluetooth gamepad data
typedef struct {
    uni_gamepad_t gamepad;
    bool connected;
    bool updated;  // Set to true when new data arrives
    char name[32];
} bt_gamepad_storage_t;

// Storage for Bluetooth keyboard data
typedef struct {
    uni_keyboard_t keyboard;
    bool connected;
    bool updated;  // Set to true when new data arrives
    char name[32];
} bt_keyboard_storage_t;

// Storage for Bluetooth mouse data
typedef struct {
    uni_mouse_t mouse;
    bool connected;
    bool updated;  // Set to true when new data arrives
    char name[32];
} bt_mouse_storage_t;

static bt_gamepad_storage_t bt_gamepads[MAX_BT_GAMEPADS] = {0};
static bt_keyboard_storage_t bt_keyboards[MAX_BT_KEYBOARDS] = {0};
static bt_mouse_storage_t bt_mice[MAX_BT_MICE] = {0};

// Store device pointer to slot mapping for each device type
static uni_hid_device_t* gamepad_device_map[MAX_BT_GAMEPADS] = {0};
static uni_hid_device_t* keyboard_device_map[MAX_BT_KEYBOARDS] = {0};
static uni_hid_device_t* mouse_device_map[MAX_BT_MICE] = {0};

#define MAX_PENDING_NAMES_BY_ADDR 8
typedef struct {
    bd_addr_t addr;
    char name[32];
    bool valid;
} pending_name_by_addr_t;
static pending_name_by_addr_t pending_names_by_addr[MAX_PENDING_NAMES_BY_ADDR] = {0};

static void store_pending_name_by_addr(bd_addr_t addr, const char* name) {
    if (!name || name[0] == '\0') {
        return;
    }
    for (int i = 0; i < MAX_PENDING_NAMES_BY_ADDR; i++) {
        if (!pending_names_by_addr[i].valid ||
            memcmp(pending_names_by_addr[i].addr, addr, 6) == 0) {
            memcpy(pending_names_by_addr[i].addr, addr, 6);
            strncpy(pending_names_by_addr[i].name, name, sizeof(pending_names_by_addr[i].name) - 1);
            pending_names_by_addr[i].name[sizeof(pending_names_by_addr[i].name) - 1] = '\0';
            pending_names_by_addr[i].valid = true;
            return;
        }
    }
}

static const char* get_pending_name_by_addr(bd_addr_t addr) {
    for (int i = 0; i < MAX_PENDING_NAMES_BY_ADDR; i++) {
        if (pending_names_by_addr[i].valid &&
            memcmp(pending_names_by_addr[i].addr, addr, 6) == 0) {
            return pending_names_by_addr[i].name;
        }
    }
    return NULL;
}

static void clear_pending_name_by_addr(bd_addr_t addr) {
    for (int i = 0; i < MAX_PENDING_NAMES_BY_ADDR; i++) {
        if (pending_names_by_addr[i].valid &&
            memcmp(pending_names_by_addr[i].addr, addr, 6) == 0) {
            pending_names_by_addr[i].valid = false;
            pending_names_by_addr[i].name[0] = '\0';
            return;
        }
    }
}

static void notify_ui_device_update(void) {
    extern void bluepad32_notify_ui_update(void);
    bluepad32_notify_ui_update();
}

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

void bluepad32_diag_log_snapshot(void) {
#if ENABLE_SERIAL_LOGGING
    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(bt_diag_last_snapshot, now) < 5000000) {
        return;
    }
    bt_diag_last_snapshot = now;

    DIAG_LOGI("[DIAG] BT storage:");
    for (int i = 0; i < MAX_BT_GAMEPADS; i++) {
        DIAG_LOGI(" GP%d{c=%d u=%d '%s'}", i,
             bt_gamepads[i].connected ? 1 : 0,
             bt_gamepads[i].updated ? 1 : 0,
             bt_gamepads[i].name[0] ? bt_gamepads[i].name : "-");
    }
    for (int i = 0; i < MAX_BT_KEYBOARDS; i++) {
        DIAG_LOGI(" KB%d{c=%d u=%d '%s'}", i,
             bt_keyboards[i].connected ? 1 : 0,
             bt_keyboards[i].updated ? 1 : 0,
             bt_keyboards[i].name[0] ? bt_keyboards[i].name : "-");
    }
    for (int i = 0; i < MAX_BT_MICE; i++) {
        DIAG_LOGI(" MS%d{c=%d u=%d '%s'}", i,
             bt_mice[i].connected ? 1 : 0,
             bt_mice[i].updated ? 1 : 0,
             bt_mice[i].name[0] ? bt_mice[i].name : "-");
    }
    DIAG_LOGI("\n");

    DIAG_LOGI("[DIAG] BT callbacks/5s: kb_in=%lu ms_in=%lu joy_in=%lu kb_drop=%lu ms_drop=%lu joy_drop=%lu joy_early=%lu\n",
         (unsigned long)bt_diag_kb_reports,
         (unsigned long)bt_diag_mouse_reports,
         (unsigned long)bt_diag_joy_reports,
         (unsigned long)bt_diag_kb_cb_drop,
         (unsigned long)bt_diag_mouse_cb_drop,
         (unsigned long)bt_diag_joy_cb_drop,
         (unsigned long)bt_diag_joy_cb_early);

    DIAG_LOGI("[DIAG] BT getters/5s: kb_ok=%lu kb_noupd=%lu ms_ok=%lu ms_noupd=%lu joy_ok=%lu joy_noupd=%lu pause_depth=%lu\n",
         (unsigned long)bt_diag_kb_get_ok,
         (unsigned long)bt_diag_kb_get_noupd,
         (unsigned long)bt_diag_mouse_get_ok,
         (unsigned long)bt_diag_mouse_get_noupd,
         (unsigned long)bt_diag_joy_get_ok,
         (unsigned long)bt_diag_joy_get_noupd,
         (unsigned long)core1_get_pause_depth());

    bt_diag_kb_reports = 0;
    bt_diag_mouse_reports = 0;
    bt_diag_joy_reports = 0;
    bt_diag_kb_cb_drop = 0;
    bt_diag_mouse_cb_drop = 0;
    bt_diag_joy_cb_drop = 0;
    bt_diag_joy_cb_early = 0;
    bt_diag_kb_get_ok = 0;
    bt_diag_kb_get_noupd = 0;
    bt_diag_mouse_get_ok = 0;
    bt_diag_mouse_get_noupd = 0;
    bt_diag_joy_get_ok = 0;
    bt_diag_joy_get_noupd = 0;
#endif
}

// Platform Overrides
static void my_platform_init(int argc, const char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    logi("bluepad32_platform: init()\n");
}

static void my_platform_on_init_complete(void) {
    logi("bluepad32_platform: on_init_complete()\n");

    // Pairing keys persist in BTstack TLV flash (pico_flash_bank at end of flash).
    // NVSettings uses the sector below the BT bank — see NVSettings.cpp.
    // Clear keys manually: right button on ATARI splash (bluepad32_delete_pairing_keys).

    // Wait a bit for HCI to be ready (the "HCI not ready" messages suggest it needs time)
    logi("Waiting for HCI to be ready...\n");
    sleep_ms(2000);  // Give HCI 2 seconds to initialize

    // Start scanning and autoconnect to supported controllers
    logi("Starting Bluetooth scanning and autoconnect...\n");
    uni_bt_start_scanning_and_autoconnect_unsafe();
    logi("Bluetooth scanning started - waiting for devices...\n");
    logi("Put your controller in pairing mode now!\n");
    DIAG_LOGI("[DIAG] firmware %s (Core1 phase/pc, BT storage+getters, HidInput consume)\n",
         PROJECT_VERSION_DISPLAY);

    // Turn off LED once init is done
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

static uni_error_t my_platform_on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
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
        DIAG_LOGI("[DIAG] Pausing Core 1 for gamepad discovery (COD=0x%04X, name='%s')\n",
                  cod, name ? name : "(null)");
        core1_pause_for_bt_enumeration();
        core1_wait_for_pause_active(20);
        bt_callback_busy_wait_ms(BT_GAMEPAD_DISCOVERY_SETTLE_MS);
        DIAG_LOGI("[DIAG] Core1 pause_depth=%lu after discovery settle\n",
                  (unsigned long)core1_get_pause_depth());
    }

    if (name && name[0] != '\0') {
        store_pending_name_by_addr(addr, name);
    }
    
    // Accept all HID devices: gamepads, keyboards, and mice
    logi("  -> Accepting device (will attempt connection)\n");
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_device_connected(uni_hid_device_t* d) {
    logi("bluepad32_platform: device connected: %p\n", d);

    // Discovery already pauses Core 1 for gamepads (COD 0x0508 / Xbox / Stadia name).
    // Do not pause again here — double-pause with a single resume in on_device_ready
    // left Core 1 stuck (depth>0) or resumed too early with the old bool flag.
}

static void my_platform_on_device_disconnected(uni_hid_device_t* d) {
    logi("bluepad32_platform: device disconnected: %p\n", d);
    
    // Resume only if enumeration was interrupted (Core 1 still paused)
    if (core1_get_pause_depth() > 0) {
        DIAG_LOGI("[DIAG] disconnect during enumeration (depth=%lu), resuming Core 1\n",
             (unsigned long)core1_get_pause_depth());
        core1_resume_after_bt_enumeration();
        DIAG_LOGI("[DIAG] Core1 pause_depth=%lu after disconnect resume\n",
             (unsigned long)core1_get_pause_depth());
    }
    
    // Clear storage for all device types (device might have been any type)
    bt_gamepad_storage_t* gp_storage = get_gamepad_storage(d);
    if (gp_storage && gp_storage->connected) {
        gp_storage->connected = false;
        gp_storage->updated = false;
        memset(&gp_storage->gamepad, 0, sizeof(gp_storage->gamepad));
        gp_storage->name[0] = '\0';
        clear_slot(d, gamepad_device_map, MAX_BT_GAMEPADS);
        extern void bluepad32_notify_unmount(void);
        bluepad32_notify_unmount();
    }
    
    bt_keyboard_storage_t* kb_storage = get_keyboard_storage(d);
    if (kb_storage && kb_storage->connected) {
        kb_storage->connected = false;
        kb_storage->updated = false;
        memset(&kb_storage->keyboard, 0, sizeof(kb_storage->keyboard));
        kb_storage->name[0] = '\0';
        clear_slot(d, keyboard_device_map, MAX_BT_KEYBOARDS);
        extern void bluepad32_notify_keyboard_unmount(void);
        bluepad32_notify_keyboard_unmount();
    }
    
    bt_mouse_storage_t* mouse_storage = get_mouse_storage(d);
    if (mouse_storage && mouse_storage->connected) {
        mouse_storage->connected = false;
        mouse_storage->updated = false;
        memset(&mouse_storage->mouse, 0, sizeof(mouse_storage->mouse));
        mouse_storage->name[0] = '\0';
        clear_slot(d, mouse_device_map, MAX_BT_MICE);
        extern void bluepad32_notify_mouse_unmount(void);
        bluepad32_notify_mouse_unmount();
    }

    notify_ui_device_update();
}

static uni_error_t my_platform_on_device_ready(uni_hid_device_t* d) {
    logi("bluepad32_platform: device ready: %p\n", d);

    bd_addr_t addr;
    uni_bt_conn_get_address(&d->conn, addr);
    const char* stored_name = get_pending_name_by_addr(addr);
    const char* device_name = NULL;
    if (uni_hid_device_has_name(d) && d->name[0] != '\0') {
        device_name = d->name;
    } else if (stored_name && stored_name[0] != '\0') {
        device_name = stored_name;
    }
    
    // Determine device type and mark appropriate storage as connected
    // Only call bluepad32_notify_mount() for gamepads (not keyboards/mice)
    if (uni_hid_device_is_gamepad(d)) {
        int slot = find_slot(d, gamepad_device_map, MAX_BT_GAMEPADS);
        DIAG_LOGI("[DIAG] gamepad ready slot=%d map=%p\n", slot, (void*)d);
        bt_gamepad_storage_t* storage = get_gamepad_storage(d);
        if (storage) {
            storage->connected = true;
            storage->updated = false;
            if (device_name && device_name[0] != '\0') {
                snprintf(storage->name, sizeof(storage->name), "%.*s",
                         (int)(sizeof(storage->name) - 1), device_name);
                clear_pending_name_by_addr(addr);
            } else {
                snprintf(storage->name, sizeof(storage->name), "Gamepad");
            }
        }
        extern void bluepad32_notify_mount(void);
        bluepad32_notify_mount();
        logi("bluepad32_platform: gamepad ready\n");
    } else if (uni_hid_device_is_keyboard(d)) {
        int slot = find_slot(d, keyboard_device_map, MAX_BT_KEYBOARDS);
        DIAG_LOGI("[DIAG] keyboard ready slot=%d map=%p\n", slot, (void*)d);
        bt_keyboard_storage_t* storage = get_keyboard_storage(d);
        if (storage) {
            storage->connected = true;
            storage->updated = false;
            if (device_name && device_name[0] != '\0') {
                snprintf(storage->name, sizeof(storage->name), "%.*s",
                         (int)(sizeof(storage->name) - 1), device_name);
                clear_pending_name_by_addr(addr);
            } else {
                snprintf(storage->name, sizeof(storage->name), "Keyboard");
            }
        }
        extern void bluepad32_notify_keyboard_mount(void);
        bluepad32_notify_keyboard_mount();
        logi("bluepad32_platform: keyboard ready\n");
    } else if (uni_hid_device_is_mouse(d)) {
        int slot = find_slot(d, mouse_device_map, MAX_BT_MICE);
        DIAG_LOGI("[DIAG] mouse ready slot=%d map=%p\n", slot, (void*)d);
        bt_mouse_storage_t* storage = get_mouse_storage(d);
        if (storage) {
            storage->connected = true;
            storage->updated = false;
            if (device_name && device_name[0] != '\0') {
                snprintf(storage->name, sizeof(storage->name), "%.*s",
                         (int)(sizeof(storage->name) - 1), device_name);
                clear_pending_name_by_addr(addr);
            } else {
                snprintf(storage->name, sizeof(storage->name), "Mouse");
            }
        }
        extern void bluepad32_notify_mouse_mount(void);
        bluepad32_notify_mouse_mount();
        logi("bluepad32_platform: mouse ready\n");
    } else {
        logi("bluepad32_platform: unknown device type ready\n");
    }

    notify_ui_device_update();

    const char* ready_type = "unknown";
    if (uni_hid_device_is_gamepad(d)) {
        ready_type = "gamepad";
    } else if (uni_hid_device_is_keyboard(d)) {
        ready_type = "keyboard";
    } else if (uni_hid_device_is_mouse(d)) {
        ready_type = "mouse";
    }
    DIAG_LOGI("[DIAG] device ready: type=%s name='%s' pause_depth=%lu\n",
         ready_type,
         (device_name && device_name[0]) ? device_name : "(none)",
         (unsigned long)core1_get_pause_depth());

    // Resume Core 1 after enumeration. Gamepad discovery pauses Core 1 while
    // BTstack may write pairing data to flash — wait before resume so Core 1
    // does not re-enter XIP emulator during flash_safe_execute (Heisenbug without
    // debug logging: 10 ms was too short; extra logi delay masked the race).
    if (core1_get_pause_depth() > 0) {
#if ENABLE_SERIAL_LOGGING
        DIAG_LOGI("[DIAG] Waiting %dms before resuming Core 1 after device ready\n",
             BT_GAMEPAD_CORE1_RESUME_DELAY_MS);
#endif
        bt_callback_busy_wait_ms(BT_GAMEPAD_CORE1_RESUME_DELAY_MS);
        core1_resume_after_bt_enumeration();
#if ENABLE_SERIAL_LOGGING
        DIAG_LOGI("[DIAG] Core1 pause_depth=%lu after device ready resume\n",
             (unsigned long)core1_get_pause_depth());
#endif
    }
    
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    switch (ctl->klass) {
        case UNI_CONTROLLER_CLASS_GAMEPAD: {
            bt_gamepad_storage_t* storage = get_gamepad_storage(d);
            if (!storage) {
                bt_diag_joy_cb_drop++;
                break;
            }
            if (!storage->connected) {
                bt_diag_joy_cb_early++;
                break;
            }
            storage->gamepad = ctl->gamepad;
            storage->updated = true;
            bt_diag_joy_reports++;
            break;
        }
        
        case UNI_CONTROLLER_CLASS_KEYBOARD: {
            bt_keyboard_storage_t* storage = get_keyboard_storage(d);
            if (!storage) {
                bt_diag_kb_cb_drop++;
                break;
            }
            if (!storage->connected) {
                storage->connected = true;
            }
            storage->keyboard = ctl->keyboard;
            storage->updated = true;
            bt_diag_kb_reports++;
            break;
        }
        
        case UNI_CONTROLLER_CLASS_MOUSE: {
            bt_mouse_storage_t* storage = get_mouse_storage(d);
            if (!storage) {
                bt_diag_mouse_cb_drop++;
                break;
            }
            if (!storage->connected) {
                storage->connected = true;
            }
            storage->mouse = ctl->mouse;
            storage->updated = true;
            bt_diag_mouse_reports++;
            break;
        }
        
        default:
            // Ignore other controller types
            logi("bluepad32_platform: unknown controller class: %d\n", ctl->klass);
            break;
    }
    bt_diag_maybe_log();
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
        uni_gamepad_t* gp = (uni_gamepad_t*)out_gamepad;
        *gp = bt_gamepads[idx].gamepad;
        bt_gamepads[idx].updated = false;
        bt_diag_joy_get_ok++;
        return true;
    }
    if (bt_gamepads[idx].connected) {
        bt_diag_joy_get_noupd++;
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
        uni_keyboard_t* kb = (uni_keyboard_t*)out_keyboard;
        *kb = bt_keyboards[idx].keyboard;
        bt_keyboards[idx].updated = false;
        bt_diag_kb_get_ok++;
        return true;
    }
    if (bt_keyboards[idx].connected) {
        bt_diag_kb_get_noupd++;
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
        uni_mouse_t* ms = (uni_mouse_t*)out_mouse;
        *ms = bt_mice[idx].mouse;
        bt_mice[idx].updated = false;
        bt_diag_mouse_get_ok++;
        return true;
    }
    if (bt_mice[idx].connected) {
        bt_diag_mouse_get_noupd++;
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

const char* bluepad32_get_device_name(char device_type, int idx) {
    if (idx < 0) {
        return NULL;
    }
    switch (device_type) {
        case 'J':
            if (idx < MAX_BT_GAMEPADS && bt_gamepads[idx].connected) {
                return bt_gamepads[idx].name;
            }
            break;
        case 'K':
            if (idx < MAX_BT_KEYBOARDS && bt_keyboards[idx].connected) {
                return bt_keyboards[idx].name;
            }
            break;
        case 'M':
            if (idx < MAX_BT_MICE && bt_mice[idx].connected) {
                return bt_mice[idx].name;
            }
            break;
        default:
            break;
    }
    return NULL;
}

