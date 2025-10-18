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

extern "C" {

// Register Xbox controller when mounted
void xinput_register_controller(uint8_t dev_addr, const xinputh_interface_t* xid_itf) {
    if (dev_addr < 8) {
        xbox_controllers[dev_addr] = xid_itf;
    }
}

// Unregister Xbox controller when unmounted
void xinput_unregister_controller(uint8_t dev_addr) {
    if (dev_addr < 8) {
        xbox_controllers[dev_addr] = NULL;
    }
}

// Forward declare ssd1306 functions
extern "C" {
    typedef struct ssd1306_t ssd1306_t;
    extern ssd1306_t disp;
    void ssd1306_clear(ssd1306_t *p);
    void ssd1306_draw_string(ssd1306_t *p, int x, int y, int scale, char *s);
    void ssd1306_show(ssd1306_t *p);
}

// Convert Xbox controller data to Atari joystick format
bool xinput_to_atari_joystick(int joystick_num, uint8_t* axis, uint8_t* button) {
    // Debug disabled for performance
    #if 0
    static uint32_t debug_check = 0;
    static bool shown_once = false;
    debug_check++;
    
    if (!shown_once || (debug_check % 1000) == 0) {
        if (!shown_once) shown_once = true;
        
        ssd1306_clear(&disp);
        
        char dbg[25];
        const xinputh_interface_t* xbox = xbox_controllers[1];
        
        snprintf(dbg, sizeof(dbg), "C:%d B:%04X", 
                 xbox ? xbox->connected : 0,
                 xbox ? xbox->pad.wButtons : 0);
        ssd1306_draw_string(&disp, 0, 0, 1, dbg);
        
        if (xbox) {
            snprintf(dbg, sizeof(dbg), "LX:%d LY:%d", 
                     xbox->pad.sThumbLX, xbox->pad.sThumbLY);
            ssd1306_draw_string(&disp, 0, 10, 1, dbg);
        }
        
        ssd1306_show(&disp);
    }
    #endif
    
    // Find first connected Xbox controller
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        const xinputh_interface_t* xbox = xbox_controllers[dev_addr];
        
        // Check if controller exists (relaxed check - don't require new_pad_data flag)
        if (xbox && xbox->connected) {
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
    }
    
    return false;
}

}  // extern "C"

