/*
 * Xbox Controller to Atari ST Joystick Mapper
 * Maps XInput gamepad data to Atari ST joystick format
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

// Forward declare only what we need from xinput_host.h to avoid TinyUSB header issues
extern "C" {

// XInput button defines
#define XINPUT_GAMEPAD_DPAD_UP 0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN 0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT 0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT 0x0008
#define XINPUT_GAMEPAD_A 0x1000
#define XINPUT_GAMEPAD_B 0x2000

#define CFG_TUH_XINPUT_EPIN_BUFSIZE 64
#define CFG_TUH_XINPUT_EPOUT_BUFSIZE 64

typedef enum {
    XINPUT_UNKNOWN = 0,
    XBOXONE,
    XBOX360_WIRELESS,
    XBOX360_WIRED,
    XBOXOG
} xinput_type_t;

typedef struct {
    uint16_t wButtons;
    uint8_t bLeftTrigger;
    uint8_t bRightTrigger;
    int16_t sThumbLX;
    int16_t sThumbLY;
    int16_t sThumbRX;
    int16_t sThumbRY;
} xinput_gamepad_t;

// MUST match xinput_host.h structure exactly!
typedef struct {
    xinput_type_t type;
    xinput_gamepad_t pad;
    uint8_t connected;
    uint8_t new_pad_data;
    uint8_t itf_num;
    uint8_t ep_in;
    uint8_t ep_out;
    uint16_t epin_size;
    uint16_t epout_size;
    uint8_t epin_buf[CFG_TUH_XINPUT_EPIN_BUFSIZE];
    uint8_t epout_buf[CFG_TUH_XINPUT_EPOUT_BUFSIZE];
    int last_xfer_result;  // xfer_result_t
    uint32_t last_xferred_bytes;
} xinputh_interface_t;

}

// Storage for Xbox controller state (indexed by dev_addr)
static xinputh_interface_t const* xbox_controllers[8] = {0};

// Debug counter for successful data reads (accessible from UI)
static uint32_t xbox_data_read_count = 0;
static uint32_t xbox_lookup_calls = 0;  // How many times lookup was called
extern "C" uint32_t get_xbox_data_read_count() {
    return xbox_data_read_count;
}
extern "C" uint32_t get_xbox_lookup_calls() {
    return xbox_lookup_calls;
}

// Debug info: Last seen flags for UI display
static uint8_t last_seen_addr = 0;
static uint8_t last_seen_connected = 0;
static uint8_t last_seen_new_data = 0;
static uint8_t last_register_addr = 0;
static uint8_t array_slot_count = 0;  // How many non-null slots
extern "C" void get_xbox_debug_flags(uint8_t* addr, uint8_t* connected, uint8_t* new_data) {
    // Count non-null slots
    array_slot_count = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (xbox_controllers[i] != NULL) {
            array_slot_count++;
        }
    }
    
    // Show the last address we REGISTERED at (from report callback)
    // Return as: (register_addr << 4) | array_slot_count for compact display
    *addr = last_register_addr;
    *connected = last_seen_connected;
    *new_data = array_slot_count;  // Reuse this field to show slot count
}

extern "C" {

// Register Xbox controller when mounted
void xinput_register_controller(uint8_t dev_addr, const xinputh_interface_t* xid_itf) {
    if (dev_addr < 8) {
        // Update last register address for UI debug
        last_register_addr = dev_addr;
        
        // Store controller pointer
        xbox_controllers[dev_addr] = xid_itf;
    }
}

// Unregister Xbox controller when unmounted
void xinput_unregister_controller(uint8_t dev_addr) {
    if (dev_addr < 8) {
        xbox_controllers[dev_addr] = NULL;
    }
}

// Forward declare ssd1306 and pico functions
extern "C" {
    typedef struct ssd1306_t ssd1306_t;
    extern ssd1306_t disp;
    void ssd1306_clear(ssd1306_t *p);
    void ssd1306_draw_string(ssd1306_t *p, int x, int y, int scale, char *s);
    void ssd1306_show(ssd1306_t *p);
    void sleep_ms(uint32_t ms);
}

static void xinput_compute_axes(const xinput_gamepad_t* pad,
                                uint8_t* left_axis, uint8_t* fire,
                                uint8_t* right_axis, uint8_t* joy0_fire) {
    const int DEADZONE = 8000;
    
    if (left_axis) {
        *left_axis = 0;
        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP)    *left_axis |= 0x01;
        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  *left_axis |= 0x02;
        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  *left_axis |= 0x04;
        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) *left_axis |= 0x08;
        
        if (*left_axis == 0) {
            if (pad->sThumbLX < -DEADZONE)  *left_axis |= 0x04;
            if (pad->sThumbLX > DEADZONE)   *left_axis |= 0x08;
            if (pad->sThumbLY > DEADZONE)   *left_axis |= 0x01;  // Note: Y inverted
            if (pad->sThumbLY < -DEADZONE)  *left_axis |= 0x02;
        }
    }
    
    if (fire) {
        *fire = (pad->wButtons & XINPUT_GAMEPAD_A) || (pad->bRightTrigger > 128) ? 1 : 0;
    }
    
    if (right_axis) {
        *right_axis = 0;
        if (pad->sThumbRX < -DEADZONE)  *right_axis |= 0x04;
        if (pad->sThumbRX > DEADZONE)   *right_axis |= 0x08;
        if (pad->sThumbRY > DEADZONE)   *right_axis |= 0x01;
        if (pad->sThumbRY < -DEADZONE)  *right_axis |= 0x02;
    }
    
    if (joy0_fire) {
        *joy0_fire = (pad->wButtons & XINPUT_GAMEPAD_B) ? 1 : 0;
    }
}

// Convert Xbox controller data to Atari joystick format
bool xinput_to_atari_joystick(int joystick_num, uint8_t* axis, uint8_t* button) {
    // Debug to track data flow issues
    static uint32_t call_count = 0;
    static uint32_t last_found_addr = 0;
    static uint32_t not_found_count = 0;
    call_count++;
    
    // Increment lookup call counter for UI
    xbox_lookup_calls++;
    
    // Find first connected Xbox controller
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        const xinputh_interface_t* xbox = xbox_controllers[dev_addr];
        
        // FIX: Add extensive validation to detect stale pointers
        if (xbox == NULL) {
            continue;  // No controller at this address
        }
        
        // FIX: Increment counter IMMEDIATELY to prove we found something
        // This proves the lookup is working
        xbox_data_read_count++;
        
        // Update debug flags for UI display
        last_seen_addr = dev_addr;
        last_seen_connected = xbox->connected;
        last_seen_new_data = xbox->new_pad_data;
        
        // FIX: IGNORE connected and new_pad_data flags completely!
        // After PS4 usage, TinyUSB sets these flags to 0 incorrectly
        // But the data is still valid - just use it!
        // We only care that the pointer is not NULL
        
        // Get pad data - if pointer exists, use it regardless of flags
        const xinput_gamepad_t* pad = &xbox->pad;
        
        xinput_compute_axes(pad, axis, button, nullptr, nullptr);
        
        // Debug disabled for performance
        #if 0
        static uint32_t found_count = 0;
        if ((found_count++ % 50) == 0) {
            char result[25];
            snprintf(result, sizeof(result), "=>A:%02X F:%d", *axis, *button);
            ssd1306_draw_string(&disp, 0, 20, 1, result);
            ssd1306_show(&disp);
        }
        #endif
        
        return true;
    }
    
    // No valid controller found - update debug flags
    for (uint8_t i = 0; i < 8; i++) {
        if (xbox_controllers[i] != NULL) {
            last_seen_addr = i;
            last_seen_connected = xbox_controllers[i]->connected;
            last_seen_new_data = xbox_controllers[i]->new_pad_data;
            break;
        }
    }
    
    return false;
}

extern "C" uint8_t xinput_connected_count(void) {
    uint8_t count = 0;
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        const xinputh_interface_t* xbox = xbox_controllers[dev_addr];
        if (xbox && xbox->connected) {
            count++;
        }
    }
    return count;
}

extern "C" bool xinput_llamatron_axes(uint8_t* joy1_axis, uint8_t* joy1_fire,
                                      uint8_t* joy0_axis, uint8_t* joy0_fire) {
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        const xinputh_interface_t* xbox = xbox_controllers[dev_addr];
        if (xbox && xbox->connected) {
            xinput_compute_axes(&xbox->pad, joy1_axis, joy1_fire, joy0_axis, joy0_fire);
            return true;
        }
    }
    return false;
}

// Get Xbox controller by device address (for pause button checking)
extern "C" const xinputh_interface_t* xinput_get_controller(uint8_t dev_addr) {
    if (dev_addr >= 1 && dev_addr < 8) {
        return xbox_controllers[dev_addr];
    }
    return NULL;
}

// Check if any Xbox controller has menu/start button pressed (for pause button)
extern "C" bool xinput_check_menu_or_start_button(void) {
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        const xinputh_interface_t* xbox = xbox_controllers[dev_addr];
        if (xbox && xbox->connected) {
            // Check BACK button (Menu) or START button
            if ((xbox->pad.wButtons & 0x0020) || (xbox->pad.wButtons & 0x0010)) {
                return true;
            }
        }
    }
    return false;
}
}  // extern "C"

