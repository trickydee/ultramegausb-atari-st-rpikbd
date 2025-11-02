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
    
    // Show first report with detailed debug - ALWAYS SHOW THIS
    if (first_report_ever) {
        first_report_ever = false;
        printf("GC: First report received (%d bytes)\n", len);
        printf("GC: Signal byte: 0x%02X\n", report[0]);
        
        // Show raw bytes on OLED for debugging - ALWAYS IN DEBUG MODE
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 0, 0, 1, (char*)"GC First Rpt!");
        
        char line[32];
        snprintf(line, sizeof(line), "Len:%d Sig:%02X", len, report[0]);
        ssd1306_draw_string(&disp, 0, 12, 1, line);
        
        // Show port 1 status bytes
        snprintf(line, sizeof(line), "P1:%02X %02X %02X", report[1], report[2], report[3]);
        ssd1306_draw_string(&disp, 0, 24, 1, line);
        
        // Show port 1 stick data
        snprintf(line, sizeof(line), "Stk:%02X %02X", report[4], report[5]);
        ssd1306_draw_string(&disp, 0, 36, 1, line);
        
        ssd1306_show(&disp);
        sleep_ms(5000);  // Show for 5 seconds
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
    
#if ENABLE_CONTROLLER_DEBUG
    // Debug output every 50 reports (~twice per second for better responsiveness)
    static uint32_t report_count = 0;
    report_count++;
    
    if ((report_count % 50) == 0) {
        ssd1306_clear(&disp);
        char line[32];
        
        // Always show report count and port status
        if (adapter->active_port != 0xFF) {
            gc_controller_input_t* ctrl = &adapter->report.port[adapter->active_port];
            
            snprintf(line, sizeof(line), "GC P%d #%lu", adapter->active_port + 1, report_count);
            ssd1306_draw_string(&disp, 0, 0, 1, line);
            
            snprintf(line, sizeof(line), "X:%02X Y:%02X", ctrl->stick_x, ctrl->stick_y);
            ssd1306_draw_string(&disp, 0, 12, 1, line);
            
            snprintf(line, sizeof(line), "B:%02X %02X", ctrl->buttons1, ctrl->buttons2);
            ssd1306_draw_string(&disp, 0, 24, 1, line);
            
            snprintf(line, sizeof(line), "Trig L:%02X R:%02X", ctrl->l_trigger, ctrl->r_trigger);
            ssd1306_draw_string(&disp, 0, 36, 1, line);
        } else {
            // Show port scan status
            snprintf(line, sizeof(line), "GC Scan #%lu", report_count);
            ssd1306_draw_string(&disp, 0, 0, 1, line);
            
            // Show all 4 port statuses
            // Type: 0=none, 1=normal (0x10), 2=wavebird (0x20)
            for (uint8_t p = 0; p < 4; p++) {
                uint8_t status_byte = *((uint8_t*)&adapter->report.port[p]);
                snprintf(line, sizeof(line), "P%d: S:%02X T:%d", 
                         p + 1, 
                         status_byte,
                         adapter->report.port[p].type);
                ssd1306_draw_string(&disp, 0, 12 + p * 10, 1, line);
            }
        }
        
        ssd1306_show(&disp);
    }
#endif
    
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
    
    // Show on OLED - match other controller style
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 10, 10, 2, (char*)"GCube");
    ssd1306_draw_string(&disp, 5, 35, 1, (char*)"USB Adapter");
    ssd1306_show(&disp);
    sleep_ms(2000);
    
    gc_adapter_t* adapter = allocate_adapter(dev_addr);
    if (adapter) {
        printf("GC: Adapter registered!\n");
        printf("GC: Sending initialization command to instance 0...\n");
        
        // Send init to instance 0
        gc_send_init(dev_addr, 0);
        
        printf("GC: Adapter address: %d\n", dev_addr);
        printf("GC: Waiting for first report...\n");
        
        // Show diagnostic info on OLED
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
    }
}

void gc_unmount_cb(uint8_t dev_addr) {
    printf("GC: Adapter unmounted at address %d\n", dev_addr);
    free_adapter(dev_addr);
}

