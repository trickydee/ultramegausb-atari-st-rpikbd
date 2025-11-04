/*
 * GameCube Controller USB Adapter - Direct USBH Implementation
 * 
 * The GameCube adapter uses RAW USB BULK endpoints, not HID class!
 * This implementation uses TinyUSB's modern usbh API directly.
 * Based on Linux driver, gc-x, and OSX driver reference implementations.
 * 
 * Endpoints (BULK type, not interrupt!):
 *   IN:  0x81 (reads 37-byte reports via bulk transfer)
 *   OUT: 0x02 (sends init/rumble commands via bulk transfer)
 * 
 * Initialization: Send 0x13 to endpoint 0x02
 * Report format: 37 bytes, first byte 0x21
 */

#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "gamecube_adapter.h"
#include "ssd1306.h"

// USB Endpoints (from reference drivers)
#define GC_EP_IN   0x81  // BULK IN endpoint
#define GC_EP_OUT  0x02  // BULK OUT endpoint

// Report format
#define GC_REPORT_SIZE  37
#define GC_SIGNAL_BYTE  0x21

// State tracking
typedef struct {
    uint8_t dev_addr;
    uint8_t itf_num;
    bool mounted;
    bool init_sent;
    bool ep_in_claimed;
    bool ep_out_claimed;
    uint8_t ep_in;
    uint8_t ep_out;
    uint8_t report_buffer[GC_REPORT_SIZE];
    gc_adapter_t adapter_state;
} gc_usbh_device_t;

static gc_usbh_device_t gc_devices[CFG_TUH_DEVICE_MAX];

//--------------------------------------------------------------------
// Helper Functions
//--------------------------------------------------------------------

static gc_usbh_device_t* find_gc_device(uint8_t dev_addr) {
    for (int i = 0; i < CFG_TUH_DEVICE_MAX; i++) {
        if (gc_devices[i].mounted && gc_devices[i].dev_addr == dev_addr) {
            return &gc_devices[i];
        }
    }
    return NULL;
}

static gc_usbh_device_t* alloc_gc_device(uint8_t dev_addr, uint8_t itf_num) {
    for (int i = 0; i < CFG_TUH_DEVICE_MAX; i++) {
        if (!gc_devices[i].mounted) {
            memset(&gc_devices[i], 0, sizeof(gc_usbh_device_t));
            gc_devices[i].dev_addr = dev_addr;
            gc_devices[i].itf_num = itf_num;
            gc_devices[i].mounted = true;
            gc_devices[i].adapter_state.dev_addr = dev_addr;
            gc_devices[i].adapter_state.connected = true;
            gc_devices[i].adapter_state.deadzone = 35;  // Default deadzone
            gc_devices[i].adapter_state.active_port = 0xFF;  // No controller yet
            return &gc_devices[i];
        }
    }
    return NULL;
}

static void free_gc_device(uint8_t dev_addr) {
    gc_usbh_device_t* dev = find_gc_device(dev_addr);
    if (dev) {
        dev->mounted = false;
    }
}

//--------------------------------------------------------------------
// Transfer Callbacks
//--------------------------------------------------------------------

// Track if callbacks are ever invoked
static volatile uint32_t g_gc_in_callbacks = 0;
static volatile uint32_t g_gc_out_callbacks = 0;

// Called when interrupt IN transfer (report read) completes
static void gc_in_xfer_cb(tuh_xfer_t* xfer) {
    g_gc_in_callbacks++;
    static uint32_t callback_count = 0;
    callback_count++;
    
    // Show on OLED that callback was called!
    if (callback_count <= 3) {
        extern ssd1306_t disp;
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 0, 0, 2, (char*)"CB CALLED!");
        char line[32];
        snprintf(line, sizeof(line), "#%lu R:%d L:%lu", 
                 callback_count, xfer->result, xfer->actual_len);
        ssd1306_draw_string(&disp, 0, 20, 1, line);
        ssd1306_show(&disp);
        sleep_ms(2000);
    }
    
    printf("GC: IN callback #%lu: result=%d, len=%lu\n", 
           callback_count, xfer->result, xfer->actual_len);
    
    gc_usbh_device_t* gc_dev = find_gc_device(xfer->daddr);
    if (!gc_dev) {
        printf("GC: ERROR - Device not found in callback!\n");
        return;
    }
    
    if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len == GC_REPORT_SIZE) {
        // Process the report
        printf("GC: Processing report...\n");
        gc_process_report(xfer->daddr, gc_dev->report_buffer, GC_REPORT_SIZE);
    } else if (xfer->result != XFER_RESULT_SUCCESS) {
        printf("GC: IN transfer failed: result=%d\n", xfer->result);
    } else if (xfer->actual_len != GC_REPORT_SIZE) {
        printf("GC: Wrong size: got %lu, expected %d\n", xfer->actual_len, GC_REPORT_SIZE);
    }
    
    // Queue next report read (continuous polling)
    tuh_xfer_t in_xfer = {
        .daddr = gc_dev->dev_addr,
        .ep_addr = gc_dev->ep_in,
        .buflen = GC_REPORT_SIZE,
        .buffer = gc_dev->report_buffer,
        .complete_cb = gc_in_xfer_cb,
        .user_data = 0
    };
    
    bool requeue_ok = tuh_edpt_xfer(&in_xfer);
    if (!requeue_ok) {
        printf("GC: ERROR - Failed to requeue IN transfer!\n");
    }
}

// Called when interrupt OUT transfer (init/rumble) completes
static void gc_out_xfer_cb(tuh_xfer_t* xfer) {
    g_gc_out_callbacks++;
    extern ssd1306_t disp;
    
    printf("GC: OUT callback: result=%d, len=%lu\n", xfer->result, xfer->actual_len);
    
    if (xfer->result == XFER_RESULT_SUCCESS) {
        printf("GC: ✓ Init command sent successfully\n");
        
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 0, 0, 2, (char*)"INIT OK!");
        ssd1306_draw_string(&disp, 0, 20, 1, (char*)"0x13 sent");
        ssd1306_draw_string(&disp, 0, 32, 1, (char*)"Wait for rpt");
        ssd1306_show(&disp);
        sleep_ms(2000);
    } else {
        printf("GC: ✗ Init command failed: result=%d\n", xfer->result);
        
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 0, 0, 2, (char*)"INIT FAIL!");
        char line[32];
        snprintf(line, sizeof(line), "Result:%d", xfer->result);
        ssd1306_draw_string(&disp, 0, 20, 1, line);
        ssd1306_show(&disp);
        sleep_ms(3000);
    }
}

//--------------------------------------------------------------------
// Mount/Unmount Handling
//--------------------------------------------------------------------

// Called when device is mounted - check if it's GameCube adapter
void tuh_mount_cb(uint8_t dev_addr) {
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    
    // Not a GameCube adapter? Let other drivers handle it
    if (!gc_is_adapter(vid, pid)) {
        return;
    }
    
    printf("\n=== GameCube Adapter Detected via USBH! ===\n");
    printf("dev_addr=%d, VID=%04X, PID=%04X\n", dev_addr, vid, pid);
    
    extern ssd1306_t disp;
    char line[32];
    
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 2, (char*)">>> USBH");
    ssd1306_draw_string(&disp, 0, 18, 2, (char*)">>> MOUNT");
    snprintf(line, sizeof(line), "Addr:%d", dev_addr);
    ssd1306_draw_string(&disp, 0, 40, 1, line);
    ssd1306_draw_string(&disp, 0, 52, 1, (char*)"Starting...");
    ssd1306_show(&disp);
    sleep_ms(3000);  // 3 seconds - hard to miss!
    
    // Allocate device (use interface 0)
    gc_usbh_device_t* gc_dev = alloc_gc_device(dev_addr, 0);
    if (!gc_dev) {
        printf("GC: ERROR - Cannot allocate device\n");
        return;
    }
    
    // IMPORTANT: Cannot do synchronous USB operations in mount callback!
    // They cause deadlock. We'll hardcode the endpoints instead.
    // From Linux driver and gc-x: GameCube adapter ALWAYS uses these endpoints
    gc_dev->ep_in = 0x81;   // Interrupt IN endpoint
    gc_dev->ep_out = 0x02;  // Interrupt OUT endpoint
    
    printf("GC: Using hardcoded endpoints: IN=0x81, OUT=0x02\n");
    
    // Set configuration to 1 (this device only has one configuration)
    printf("GC: Setting configuration 1...\n");
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 1, (char*)"GC: Set Config");
    ssd1306_show(&disp);
    sleep_ms(500);
    
    // Note: Configuration is usually already set by TinyUSB enumeration
    // But we'll set interface 0 explicitly
    printf("GC: Setting interface 0...\n");
    
    // We can't do synchronous calls here, so we'll skip explicit interface setting
    // and rely on TinyUSB having done it during enumeration
    
    // Create endpoint descriptors manually
    // Reference drivers disagree on type (Linux/gc-x use interrupt, OSX uses bulk)
    // Try INTERRUPT first (matches 2/3 drivers), we can try BULK if it fails
    tusb_desc_endpoint_t ep_in_desc_int = {
        .bLength = sizeof(tusb_desc_endpoint_t),
        .bDescriptorType = TUSB_DESC_ENDPOINT,
        .bEndpointAddress = 0x81,
        .bmAttributes = { .xfer = TUSB_XFER_INTERRUPT },
        .wMaxPacketSize = 37,
        .bInterval = 8
    };
    
    tusb_desc_endpoint_t ep_out_desc_int = {
        .bLength = sizeof(tusb_desc_endpoint_t),
        .bDescriptorType = TUSB_DESC_ENDPOINT,
        .bEndpointAddress = 0x02,
        .bmAttributes = { .xfer = TUSB_XFER_INTERRUPT },
        .wMaxPacketSize = 5,
        .bInterval = 8
    };
    
    tusb_desc_endpoint_t ep_in_desc_bulk = {
        .bLength = sizeof(tusb_desc_endpoint_t),
        .bDescriptorType = TUSB_DESC_ENDPOINT,
        .bEndpointAddress = 0x81,
        .bmAttributes = { .xfer = TUSB_XFER_BULK },
        .wMaxPacketSize = 37,
        .bInterval = 0
    };
    
    tusb_desc_endpoint_t ep_out_desc_bulk = {
        .bLength = sizeof(tusb_desc_endpoint_t),
        .bDescriptorType = TUSB_DESC_ENDPOINT,
        .bEndpointAddress = 0x02,
        .bmAttributes = { .xfer = TUSB_XFER_BULK },
        .wMaxPacketSize = 5,
        .bInterval = 0
    };
    
    // Try to open endpoints
    printf("GC: Opening endpoints (trying INTERRUPT first)...\n");
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 1, (char*)"GC: Open EPs");
    ssd1306_draw_string(&disp, 0, 12, 1, (char*)"Try INTERRUPT");
    ssd1306_show(&disp);
    sleep_ms(500);
    
    bool ep_in_ok = tuh_edpt_open(dev_addr, &ep_in_desc_int);
    bool ep_out_ok = tuh_edpt_open(dev_addr, &ep_out_desc_int);
    
    printf("GC: EP IN (interrupt) open result: %d\n", ep_in_ok);
    printf("GC: EP OUT (interrupt) open result: %d\n", ep_out_ok);
    
    // If INTERRUPT failed, try BULK (OSX driver approach)
    if (!ep_in_ok || !ep_out_ok) {
        printf("GC: INTERRUPT open failed, trying BULK...\n");
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 0, 0, 1, (char*)"Try BULK");
        ssd1306_show(&disp);
        sleep_ms(500);
        
        if (!ep_in_ok) {
            ep_in_ok = tuh_edpt_open(dev_addr, &ep_in_desc_bulk);
            printf("GC: EP IN (bulk) open result: %d\n", ep_in_ok);
        }
        
        if (!ep_out_ok) {
            ep_out_ok = tuh_edpt_open(dev_addr, &ep_out_desc_bulk);
            printf("GC: EP OUT (bulk) open result: %d\n", ep_out_ok);
        }
    }
    
    if (ep_in_ok) {
        gc_dev->ep_in_claimed = true;
        printf("GC: ✓ Opened EP IN\n");
    } else {
        printf("GC: ✗ Failed to open EP IN with both methods!\n");
    }
    
    if (ep_out_ok) {
        gc_dev->ep_out_claimed = true;
        printf("GC: ✓ Opened EP OUT\n");
    } else {
        printf("GC: ✗ Failed to open EP OUT with both methods!\n");
    }
    
    // Show results
    ssd1306_clear(&disp);
    if (gc_dev->ep_in_claimed && gc_dev->ep_out_claimed) {
        ssd1306_draw_string(&disp, 0, 0, 2, (char*)"EPs OK!");
        ssd1306_draw_string(&disp, 0, 20, 1, (char*)"IN+OUT opened");
        printf("GC: ✓ Both endpoints opened successfully\n");
    } else {
        ssd1306_draw_string(&disp, 0, 0, 2, (char*)"EP FAIL!");
        snprintf(line, sizeof(line), "IN:%d OUT:%d", ep_in_ok, ep_out_ok);
        ssd1306_draw_string(&disp, 0, 20, 1, line);
        printf("GC: FATAL - Cannot claim endpoints\n");
    }
    ssd1306_show(&disp);
    sleep_ms(2000);
    
    if (!gc_dev->ep_in_claimed || !gc_dev->ep_out_claimed) {
        free_gc_device(dev_addr);
        return;
    }
    
#if ENABLE_CONTROLLER_DEBUG
    // Show splash screen (debug mode only)
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 10, 10, 2, (char*)"GCube");
    ssd1306_draw_string(&disp, 5, 35, 1, (char*)"USB Adapter");
    ssd1306_show(&disp);
    sleep_ms(2000);
#endif
    
    // Send initialization command 0x13
    printf("GC: Sending init 0x13...\n");
    static uint8_t init_cmd = 0x13;
    
    tuh_xfer_t init_xfer = {
        .daddr = dev_addr,
        .ep_addr = gc_dev->ep_out,
        .buflen = 1,
        .buffer = &init_cmd,
        .complete_cb = gc_out_xfer_cb,
        .user_data = 0
    };
    
    if (tuh_edpt_xfer(&init_xfer)) {
        gc_dev->init_sent = true;
    } else {
        printf("GC: WARNING - Init transfer queue failed!\n");
    }
    
#if ENABLE_CONTROLLER_DEBUG
    // Show waiting screen (debug mode only)
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 1, (char*)"GC Init Sent");
    snprintf(line, sizeof(line), "Addr:%d", dev_addr);
    ssd1306_draw_string(&disp, 0, 12, 1, line);
    ssd1306_draw_string(&disp, 0, 24, 1, (char*)"PC mode?");
    ssd1306_draw_string(&disp, 0, 36, 1, (char*)"Ctrl plugged?");
    ssd1306_draw_string(&disp, 0, 48, 1, (char*)"Listening...");
    ssd1306_show(&disp);
    sleep_ms(2000);  // Reduced to 2 seconds
#endif
    
#if ENABLE_CONTROLLER_DEBUG
    // CHECKPOINT: Show we're continuing past "Listening..." screen (debug mode only)
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 2, (char*)"CHK 1:");
    ssd1306_draw_string(&disp, 0, 20, 1, (char*)"Past listen");
    ssd1306_draw_string(&disp, 0, 32, 1, (char*)"Queue IN xfer");
    ssd1306_show(&disp);
    sleep_ms(2000);
#endif
    
    // Start receiving reports
    printf("GC: Queueing first IN transfer...\n");
    tuh_xfer_t in_xfer = {
        .daddr = dev_addr,
        .ep_addr = gc_dev->ep_in,
        .buflen = GC_REPORT_SIZE,
        .buffer = gc_dev->report_buffer,
        .complete_cb = gc_in_xfer_cb,
        .user_data = 0
    };
    
    bool xfer_ok = tuh_edpt_xfer(&in_xfer);
    printf("GC: IN transfer queue result: %d\n", xfer_ok);
    
    if (!xfer_ok) {
        printf("GC: ERROR - Failed to queue IN transfer!\n");
#if ENABLE_CONTROLLER_DEBUG
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 0, 0, 2, (char*)"XFER FAIL");
        ssd1306_draw_string(&disp, 0, 20, 1, (char*)"Can't queue");
        ssd1306_draw_string(&disp, 0, 32, 1, (char*)"IN transfer!");
        ssd1306_show(&disp);
        sleep_ms(5000);
#endif
        free_gc_device(dev_addr);
        return;
    }
    
    printf("GC: ✓ IN transfer queued successfully\n");
    
#if ENABLE_CONTROLLER_DEBUG
    // Show success screen - IN transfer is queued! (debug mode only)
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 2, (char*)"QUEUED!");
    ssd1306_draw_string(&disp, 0, 20, 1, (char*)"IN xfer ready");
    ssd1306_draw_string(&disp, 0, 32, 1, (char*)"Notifying app");
    ssd1306_show(&disp);
    sleep_ms(2000);
#endif
    
    printf("GC: Adapter fully initialized!\n");
    
    // Notify C++ layer that a joystick was mounted
    // Use helper function to increment joy_count (can't access C++ static directly from C)
    extern void gc_notify_mount(void);  // Defined in HidInput.cpp
    gc_notify_mount();
    printf("GC: Joystick counter incremented, UI notified\n");
    
#if ENABLE_CONTROLLER_DEBUG
    // Final success screen (debug mode only)
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 2, (char*)"SETUP OK!");
    ssd1306_draw_string(&disp, 0, 20, 1, (char*)"Joy counter++");
    ssd1306_draw_string(&disp, 0, 32, 1, (char*)"Press center");
    ssd1306_draw_string(&disp, 0, 44, 1, (char*)"to see count");
    ssd1306_show(&disp);
    sleep_ms(3000);
#endif
    
    printf("GC: Mount complete!\n\n");
}

// Called when device is unmounted
void tuh_umount_cb(uint8_t dev_addr) {
    gc_usbh_device_t* gc_dev = find_gc_device(dev_addr);
    if (gc_dev) {
        printf("GC: GameCube adapter unmounted: dev_addr=%d\n", dev_addr);
        gc_unmount_cb(dev_addr);
        
        // Notify C++ layer that joystick was unmounted
        extern void gc_notify_unmount(void);
        gc_notify_unmount();
        
        free_gc_device(dev_addr);
    }
}

//--------------------------------------------------------------------
// Public API Implementation
//--------------------------------------------------------------------

gc_adapter_t* gc_get_adapter_vendor(uint8_t dev_addr) {
    gc_usbh_device_t* gc_dev = find_gc_device(dev_addr);
    if (gc_dev && gc_dev->mounted) {
        return &gc_dev->adapter_state;
    }
    return NULL;
}

// Poll function - to be called from main loop 
// Shows callback status and checks if we need to re-queue transfers
void gc_vendor_poll(void) {
    static uint32_t poll_count = 0;
    static uint32_t last_shown_in = 0;
    static uint32_t last_shown_out = 0;
    poll_count++;
    
    // Every 100 polls (~1 second), check callback status
    if ((poll_count % 100) == 1 && poll_count > 1) {
        // Check if callback counts changed
        if (g_gc_in_callbacks != last_shown_in || g_gc_out_callbacks != last_shown_out) {
            extern ssd1306_t disp;
            ssd1306_clear(&disp);
            ssd1306_draw_string(&disp, 0, 0, 2, (char*)"CB STATUS");
            char line[32];
            snprintf(line, sizeof(line), "IN:%lu OUT:%lu", g_gc_in_callbacks, g_gc_out_callbacks);
            ssd1306_draw_string(&disp, 0, 20, 1, line);
            ssd1306_draw_string(&disp, 0, 32, 1, (char*)"Callbacks OK!");
            ssd1306_show(&disp);
            sleep_ms(2000);
            
            last_shown_in = g_gc_in_callbacks;
            last_shown_out = g_gc_out_callbacks;
        } else if (poll_count == 101) {
            // After 1 second, if no callbacks, show warning
            extern ssd1306_t disp;
            ssd1306_clear(&disp);
            ssd1306_draw_string(&disp, 0, 0, 2, (char*)"NO CB!");
            ssd1306_draw_string(&disp, 0, 20, 1, (char*)"IN:0 OUT:0");
            ssd1306_draw_string(&disp, 0, 32, 1, (char*)"Callbacks not");
            ssd1306_draw_string(&disp, 0, 44, 1, (char*)"firing!");
            ssd1306_show(&disp);
            sleep_ms(3000);
        }
    }
}

