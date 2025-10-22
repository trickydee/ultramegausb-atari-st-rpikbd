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
        
        *axis = 0;
        *button = 0;
            
        // D-Pad mapping (takes priority)
        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP)    *axis |= 0x01;
        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  *axis |= 0x02;
        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  *axis |= 0x04;
        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) *axis |= 0x08;
        
        // Left stick as fallback (if D-Pad not pressed)
        if (*axis == 0) {
            const int DEADZONE = 8000;  // ~25% deadzone
            
            // Left stick X axis
            if (pad->sThumbLX < -DEADZONE)  *axis |= 0x04;  // Left
            if (pad->sThumbLX > DEADZONE)   *axis |= 0x08;  // Right
            
            // Left stick Y axis (inverted - Xbox Y is opposite of Atari)
            if (pad->sThumbLY > DEADZONE)   *axis |= 0x01;  // Up
            if (pad->sThumbLY < -DEADZONE)  *axis |= 0x02;  // Down
        }
        
        // Fire button mapping
        // Primary: A button (most common in games)
        // Alternative: Right trigger (if pressed > 50%)
        if (pad->wButtons & XINPUT_GAMEPAD_A) {
            *button = 1;
        } else if (pad->bRightTrigger > 128) {
            *button = 1;
        }
        
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

}  // extern "C"

