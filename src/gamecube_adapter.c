/*
 * Atari ST RP2040 IKBD Emulator - GameCube Controller USB Adapter Support
 * Copyright (C) 2025
 * 
 * GameCube controller USB adapter implementation
 * Based on GCN_Adapter-Driver reference implementation
 */

#include "gamecube_adapter.h"
#include "config.h"
#include "tusb.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>

//--------------------------------------------------------------------
// Adapter Storage
//--------------------------------------------------------------------

#define MAX_GC_ADAPTERS  2

static gc_adapter_t adapters[MAX_GC_ADAPTERS];
static uint8_t adapter_count = 0;

//--------------------------------------------------------------------
// Helper Functions
//--------------------------------------------------------------------

static gc_adapter_t* find_adapter_by_addr(uint8_t dev_addr) {
    for (uint8_t i = 0; i < adapter_count; i++) {
        if (adapters[i].dev_addr == dev_addr && adapters[i].connected) {
            return &adapters[i];
        }
    }
    return NULL;
}

static gc_adapter_t* allocate_adapter(uint8_t dev_addr) {
    if (adapter_count >= MAX_GC_ADAPTERS) {
        printf("GC: Max adapters reached\n");
        return NULL;
    }
    
    gc_adapter_t* adapter = &adapters[adapter_count++];
    memset(adapter, 0, sizeof(gc_adapter_t));
    adapter->dev_addr = dev_addr;
    adapter->connected = true;
    adapter->deadzone = 35;  // Default from reference driver
    adapter->active_port = 0xFF;  // No active port yet
    
    return adapter;
}

static void free_adapter(uint8_t dev_addr) {
    for (uint8_t i = 0; i < adapter_count; i++) {
        if (adapters[i].dev_addr == dev_addr) {
            for (uint8_t j = i; j < adapter_count - 1; j++) {
                adapters[j] = adapters[j + 1];
            }
            adapter_count--;
            break;
        }
    }
}

//--------------------------------------------------------------------
// Public API Implementation
//--------------------------------------------------------------------

bool gc_is_adapter(uint16_t vid, uint16_t pid) {
    if (vid != GAMECUBE_VENDOR_ID) {
        return false;
    }
    
    return (pid == GAMECUBE_ADAPTER_PID);
}

bool gc_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len) {
    static bool first_report_ever = true;
    static uint32_t total_reports = 0;
    extern ssd1306_t disp;
    
    total_reports++;
    
    gc_adapter_t* adapter = find_adapter_by_addr(dev_addr);
    if (!adapter) {
        printf("GC: Adapter %d not found, allocating...\n", dev_addr);
        adapter = allocate_adapter(dev_addr);
        if (!adapter) {
            return false;
        }
    }
    
    // Show we're getting reports
    if ((total_reports % 100) == 0) {
        printf("GC: Received %lu reports, len=%d\n", total_reports, len);
    }
    
    // GameCube adapter reports should be 37 bytes
    if (len < 37) {
        if ((total_reports % 100) == 0) {
            printf("GC: Report too short (%d bytes, expected 37)\n", len);
        }
        return false;
    }
    
    // Check signal byte (should be 0x21)
    if (report[0] != 0x21) {
        if ((total_reports % 100) == 0) {
            printf("GC: Invalid signal byte: 0x%02X (expected 0x21)\n", report[0]);
        }
        return false;
    }
    
    // Show first report with detailed debug
    if (first_report_ever) {
        first_report_ever = false;
        printf("GC: First report received (%d bytes)\n", len);
        printf("GC: Signal byte: 0x%02X\n", report[0]);
        
// Debug splash screen removed - no longer needed
    }
    
    // Parse the report (copy to adapter structure)
    memcpy(&adapter->report, report, sizeof(gc_adapter_report_t) < len ? 
           sizeof(gc_adapter_report_t) : len);
    
    // Find first active/powered port if we haven't yet
    // Linux driver: type is in bits 4-5 (0x10=normal, 0x20=wavebird)
    // If type != 0, controller is connected
    if (adapter->active_port == 0xFF) {
        for (uint8_t port = 0; port < 4; port++) {
            // Check type bits (1=normal, 2=wavebird)
            if (adapter->report.port[port].type != 0) {
                adapter->active_port = port;
                printf("GC: Controller detected on port %d (type=%d)!\n", port + 1, adapter->report.port[port].type);
                break;
            }
        }
    }
    
// GameCube debug screen removed - no longer needed in production
    
    return true;
}

gc_adapter_t* gc_get_adapter(uint8_t dev_addr) {
    return find_adapter_by_addr(dev_addr);
}

void gc_to_atari(const gc_adapter_t* gc, uint8_t joystick_num,
                 uint8_t* direction, uint8_t* fire) {
    static uint32_t debug_count = 0;
    debug_count++;
    
    if (!gc || !direction || !fire) {
        if (debug_count <= 2) {
            printf("GC: gc_to_atari() NULL pointer! gc=%p dir=%p fire=%p\n", gc, direction, fire);
        }
        return;
    }
    
    *direction = 0;
    *fire = 0;
    
    // Check if we have an active controller
    if (gc->active_port == 0xFF || gc->active_port >= 4) {
        if (debug_count <= 2) {
            printf("GC: gc_to_atari() no active port (active_port=%d)\n", gc->active_port);
        }
        return;
    }
    
    const gc_controller_input_t* ctrl = &gc->report.port[gc->active_port];
    
    // Check if controller is still connected
    // Type: 0=disconnected, 1=normal (0x10), 2=wavebird (0x20)
    if (ctrl->type == 0) {
        if (debug_count <= 2) {
            printf("GC: gc_to_atari() controller type=0 (disconnected)\n");
        }
        return;
    }
    
    // Debug input data
    if (debug_count <= 3) {
        printf("GC: gc_to_atari() port=%d type=%d stick_x=%02X stick_y=%02X btns=%02X %02X\n",
               gc->active_port, ctrl->type, ctrl->stick_x, ctrl->stick_y, 
               ctrl->buttons1, ctrl->buttons2);
    }
    
    // Convert analog stick to directions (main stick)
    // GameCube sticks are 0-255 with 127 as center
    // Y axis is inverted in raw data (reference driver inverts it)
    int8_t stick_x = (int8_t)(ctrl->stick_x - 127);
    int8_t stick_y = (int8_t)(127 - ctrl->stick_y);  // Invert Y
    
    // Apply deadzone
    if (stick_x < -gc->deadzone || stick_x > gc->deadzone ||
        stick_y < -gc->deadzone || stick_y > gc->deadzone) {
        
        if (stick_y < -gc->deadzone) *direction |= 0x01;  // Up
        if (stick_y > gc->deadzone)  *direction |= 0x02;  // Down
        if (stick_x < -gc->deadzone) *direction |= 0x04;  // Left
        if (stick_x > gc->deadzone)  *direction |= 0x08;  // Right
    }
    
    // D-Pad takes priority if pressed
    // buttons1: D_UP=0x80, D_DOWN=0x40, D_RIGHT=0x20, D_LEFT=0x10
    if (ctrl->buttons1 & (GC_BTN_DPAD_UP | GC_BTN_DPAD_DOWN | GC_BTN_DPAD_LEFT | GC_BTN_DPAD_RIGHT)) {
        *direction = 0;
        
        if (ctrl->buttons1 & GC_BTN_DPAD_UP)    *direction |= 0x01;
        if (ctrl->buttons1 & GC_BTN_DPAD_DOWN)  *direction |= 0x02;
        if (ctrl->buttons1 & GC_BTN_DPAD_LEFT)  *direction |= 0x04;
        if (ctrl->buttons1 & GC_BTN_DPAD_RIGHT) *direction |= 0x08;
    }
    
    // Fire button mapping
    // A button is most common fire button on GameCube
    if (ctrl->buttons1 & GC_BTN_A) {
        *fire = 1;
    }
    // B button as alternative
    else if (ctrl->buttons1 & GC_BTN_B) {
        *fire = 1;
    }
    
    // Debug output values
    if (debug_count <= 3 || (*direction != 0) || (*fire != 0)) {
        if (debug_count <= 5 || (debug_count % 50) == 0) {
            printf("GC: OUTPUT → direction=0x%02X fire=%d\n", *direction, *fire);
        }
    }
}

// Compute axes for Llamatron mode (dual-stick support)
static void gc_compute_axes(const gc_adapter_t* gc,
                             uint8_t* left_axis, uint8_t* fire,
                             uint8_t* right_axis, uint8_t* joy0_fire) {
    if (!gc || gc->active_port == 0xFF || gc->active_port >= 4) {
        return;
    }
    
    const gc_controller_input_t* ctrl = &gc->report.port[gc->active_port];
    
    // Check if controller is connected
    if (ctrl->type == 0) {
        return;
    }
    
    // Left stick (main stick) for Joy1
    if (left_axis) {
        *left_axis = 0;
        
        int8_t stick_x = (int8_t)(ctrl->stick_x - 127);
        int8_t stick_y = (int8_t)(127 - ctrl->stick_y);  // Invert Y
        
        // Apply deadzone
        if (stick_x < -gc->deadzone || stick_x > gc->deadzone ||
            stick_y < -gc->deadzone || stick_y > gc->deadzone) {
            
            if (stick_y < -gc->deadzone) *left_axis |= 0x01;  // Up
            if (stick_y > gc->deadzone)  *left_axis |= 0x02;  // Down
            if (stick_x < -gc->deadzone) *left_axis |= 0x04;  // Left
            if (stick_x > gc->deadzone)  *left_axis |= 0x08;  // Right
        }
        
        // D-Pad takes priority if pressed
        if (ctrl->buttons1 & (GC_BTN_DPAD_UP | GC_BTN_DPAD_DOWN | GC_BTN_DPAD_LEFT | GC_BTN_DPAD_RIGHT)) {
            *left_axis = 0;
            if (ctrl->buttons1 & GC_BTN_DPAD_UP)    *left_axis |= 0x01;
            if (ctrl->buttons1 & GC_BTN_DPAD_DOWN)  *left_axis |= 0x02;
            if (ctrl->buttons1 & GC_BTN_DPAD_LEFT)  *left_axis |= 0x04;
            if (ctrl->buttons1 & GC_BTN_DPAD_RIGHT) *left_axis |= 0x08;
        }
    }
    
    // B button for Joy1 fire
    if (fire) {
        *fire = (ctrl->buttons1 & GC_BTN_B) ? 1 : 0;
    }
    
    // Right stick (C-stick) for Joy0
    if (right_axis) {
        *right_axis = 0;
        
        int8_t c_stick_x = (int8_t)(ctrl->c_stick_x - 127);
        int8_t c_stick_y = (int8_t)(127 - ctrl->c_stick_y);  // Invert Y
        
        // Apply deadzone
        if (c_stick_x < -gc->deadzone || c_stick_x > gc->deadzone ||
            c_stick_y < -gc->deadzone || c_stick_y > gc->deadzone) {
            
            if (c_stick_y < -gc->deadzone) *right_axis |= 0x01;  // Up
            if (c_stick_y > gc->deadzone)  *right_axis |= 0x02;  // Down
            if (c_stick_x < -gc->deadzone) *right_axis |= 0x04;  // Left
            if (c_stick_x > gc->deadzone)  *right_axis |= 0x08;  // Right
        }
    }
    
    // A button for Joy0 fire
    if (joy0_fire) {
        *joy0_fire = (ctrl->buttons1 & GC_BTN_A) ? 1 : 0;
    }
}

uint8_t gc_connected_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < adapter_count; i++) {
        if (adapters[i].connected && adapters[i].active_port != 0xFF) {
            count++;
        }
    }
    return count;
}

bool gc_llamatron_axes(uint8_t* joy1_axis, uint8_t* joy1_fire,
                       uint8_t* joy0_axis, uint8_t* joy0_fire) {
    // Find first connected adapter with an active controller
    for (uint8_t i = 0; i < adapter_count; i++) {
        if (adapters[i].connected && adapters[i].active_port != 0xFF) {
            gc_compute_axes(&adapters[i], joy1_axis, joy1_fire, joy0_axis, joy0_fire);
            return true;
        }
    }
    return false;
}

void gc_set_deadzone(uint8_t dev_addr, int16_t deadzone) {
    gc_adapter_t* adapter = find_adapter_by_addr(dev_addr);
    if (adapter) {
        adapter->deadzone = deadzone;
        printf("GC: Deadzone set to %d for adapter %d\n", deadzone, dev_addr);
    }
}

// Send initialization command to a specific instance
void gc_send_init(uint8_t dev_addr, uint8_t instance) {
    printf("GC: Sending init to addr=%d, inst=%d\n", dev_addr, instance);
    
    // GameCube adapter initialization command
    // Based on gc-x and Dolphin: send 0x13 to start adapter
    // This is NOT the rumble command (rumble is 0x11)
    static const uint8_t gc_init_command[] = {
        0x13  // Enable adapter / start sending reports
    };
    
    // Send as output report to activate the adapter
    bool result = tuh_hid_set_report(dev_addr, instance,
                                      0,            // report_id (0 for single byte)
                                      HID_REPORT_TYPE_OUTPUT,
                                      (uint8_t*)gc_init_command, 
                                      sizeof(gc_init_command));
    
    if (result) {
        printf("GC: Init 0x13 sent to instance %d OK\n", instance);
    } else {
        printf("GC: WARNING - Init 0x13 to instance %d failed!\n", instance);
    }
}

void gc_mount_cb(uint8_t dev_addr) {
    extern ssd1306_t disp;
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  🎮 GAMECUBE CONTROLLER ADAPTER DETECTED!\n");
    printf("  Device Address: %d\n", dev_addr);
    printf("  \n");
    printf("  Supports up to 4 GameCube controllers\n");
    printf("  Will use first connected controller\n");
    printf("  \n");
    printf("  Make sure adapter is in PC MODE!\n");
    printf("  \n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("\n");
    
#if ENABLE_CONTROLLER_DEBUG
    // Show on OLED - match other controller style (debug mode only)
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 10, 10, 2, (char*)"GCube");
    ssd1306_draw_string(&disp, 5, 35, 1, (char*)"USB Adapter");
    ssd1306_show(&disp);
    sleep_ms(2000);
#endif
    
    gc_adapter_t* adapter = allocate_adapter(dev_addr);
    if (adapter) {
        printf("GC: Adapter registered!\n");
        printf("GC: Sending initialization command to instance 0...\n");
        
        // Send init to instance 0
        gc_send_init(dev_addr, 0);
        
        printf("GC: Adapter address: %d\n", dev_addr);
        printf("GC: Waiting for first report...\n");
        
#if ENABLE_CONTROLLER_DEBUG
        // Show diagnostic info on OLED (debug mode only)
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 0, 0, 1, (char*)"GC Init Sent");
        
        char line[32];
        snprintf(line, sizeof(line), "Addr:%d", dev_addr);
        ssd1306_draw_string(&disp, 0, 12, 1, line);
        
        ssd1306_draw_string(&disp, 0, 24, 1, (char*)"PC mode?");
        ssd1306_draw_string(&disp, 0, 36, 1, (char*)"Ctrl plugged?");
        ssd1306_draw_string(&disp, 0, 48, 1, (char*)"Waiting...");
        
        ssd1306_show(&disp);
        sleep_ms(5000);  // Wait 5 seconds to see this message
#endif
    }
}

void gc_unmount_cb(uint8_t dev_addr) {
    printf("GC: Adapter unmounted at address %d\n", dev_addr);
    free_adapter(dev_addr);
}

