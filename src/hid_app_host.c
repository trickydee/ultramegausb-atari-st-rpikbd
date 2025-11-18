/* 
 * HID Application Host - Adapter for official TinyUSB
 * This provides backward compatibility with the custom TinyUSB fork API
 */

#include "tusb.h"
#include "hid_app_host.h"
#include "version.h"  // Project version number
// xinput.h removed - using official xinput_host.h driver now
#include "ps3_controller.h"
#include "gamecube_adapter.h"
#include "ps4_controller.h"
#include "switch_controller.h"
#include "stadia_controller.h"
#include "ssd1306.h"  // For OLED debug display
#include <string.h>

// Structure to track HID devices
typedef struct {
  uint8_t               dev_addr;
  uint8_t               instance;
  HID_TYPE              hid_type;
  bool                  mounted;
  bool                  has_report_info;
  HID_ReportInfo_t      report_info;
  uint16_t              report_size;
  uint8_t               report_buffer[64];
  void*                 report_dest;  // Where to copy report when it arrives
  bool                  report_pending;
} hidh_device_t;

// Size array for HID interfaces, not just devices (devices can have multiple interfaces)
static hidh_device_t hid_devices[CFG_TUH_HID];
static HID_TYPE filter_type = HID_UNDEFINED;

// Debug counters (can be read externally)
static uint32_t debug_mount_calls = 0;
static uint32_t debug_report_calls = 0;
static uint32_t debug_report_copied = 0;
static uint32_t debug_unmount_calls = 0;
static uint8_t debug_last_dev_addr = 0;
static uint8_t debug_last_instance = 0;
static uint8_t debug_active_devices = 0;

// Track whether we've already notified the app layer for Stadia on first report
static bool stadia_notified[CFG_TUSB_HOST_DEVICE_MAX] = {0};

// Accessor functions for debug counters
uint32_t hid_debug_get_mount_calls(void) { return debug_mount_calls; }
uint32_t hid_debug_get_report_calls(void) { return debug_report_calls; }
uint32_t hid_debug_get_report_copied(void) { return debug_report_copied; }
uint32_t hid_debug_get_unmount_calls(void) { return debug_unmount_calls; }
uint32_t hid_debug_get_active_devices(void) { return debug_active_devices; }
uint32_t hid_debug_get_last_addr_inst(void) { return (debug_last_dev_addr << 8) | debug_last_instance; }

// Forward declaration
static hidh_device_t* find_device_by_inst(uint8_t dev_addr, uint8_t instance);

// Find device by address (supports multi-interface device keys)
static hidh_device_t* find_device(uint8_t dev_addr) {
  // Decode multi-interface device key:
  // Mouse keys are addr + 128, so decode back to actual address
  uint8_t actual_addr = (dev_addr >= 128) ? (dev_addr - 128) : dev_addr;
  
  // For mouse keys (>= 128), find the MOUSE device at that address
  if (dev_addr >= 128) {
    for (int i = 0; i < CFG_TUH_HID; i++) {
      if (hid_devices[i].dev_addr == actual_addr && 
          hid_devices[i].mounted &&
          hid_devices[i].hid_type == HID_MOUSE) {
        return &hid_devices[i];
      }
    }
    return NULL;
  }
  
  // For normal addresses, find first matching device
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (hid_devices[i].dev_addr == actual_addr && hid_devices[i].mounted) {
      return &hid_devices[i];
    }
  }
  return NULL;
}

// Find device by address and instance
static hidh_device_t* find_device_by_inst(uint8_t dev_addr, uint8_t instance) {
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (hid_devices[i].dev_addr == dev_addr && 
        hid_devices[i].instance == instance &&
        hid_devices[i].mounted) {
      return &hid_devices[i];
    }
  }
  return NULL;
}

// Allocate a new device slot
static hidh_device_t* alloc_device(uint8_t dev_addr, uint8_t instance) {
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (!hid_devices[i].mounted) {
      memset(&hid_devices[i], 0, sizeof(hidh_device_t));
      hid_devices[i].dev_addr = dev_addr;
      hid_devices[i].instance = instance;
      hid_devices[i].mounted = true;
      return &hid_devices[i];
    }
  }
  return NULL;
}

// Free a device slot
static void free_device(uint8_t dev_addr) {
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (hid_devices[i].dev_addr == dev_addr) {
      memset(&hid_devices[i], 0, sizeof(hidh_device_t));
      return;
    }
  }
}

// HID parser filter callback
bool CALLBACK_HIDParser_FilterHIDReportItem(HID_ReportItem_t* const item)
{
  // Attempt to determine what type of device this is
  if (filter_type == HID_UNDEFINED) {
    // Iterate through the item's collection path
    for (HID_CollectionPath_t* path = item->CollectionPath; path != NULL; path = path->Parent)
    {
      if ((path->Usage.Page  == USAGE_PAGE_GENERIC_DCTRL) && (path->Usage.Usage == USAGE_JOYSTICK)) {
        filter_type = HID_JOYSTICK;
        break;
      }
      else if ((path->Usage.Page  == USAGE_PAGE_GENERIC_DCTRL) && (path->Usage.Usage == USAGE_MOUSE)) {
        filter_type = HID_MOUSE;
        break;
      }
    }
    
    // Additional detection: If we see X and Y axes with buttons, it's likely a mouse
    // This helps detect wireless mice on Logitech Unifying receivers which may
    // not use the standard USAGE_MOUSE collection
    if (filter_type == HID_UNDEFINED &&
        item->Attributes.Usage.Page == USAGE_PAGE_GENERIC_DCTRL) {
      if (item->Attributes.Usage.Usage == USAGE_X || 
          item->Attributes.Usage.Usage == USAGE_Y) {
        // Found X or Y axis - likely a mouse (or joystick, but we'll detect buttons later)
        filter_type = HID_MOUSE;
      }
    }
  }
  
  if (filter_type == HID_JOYSTICK || filter_type == HID_MOUSE) {
    return ((item->Attributes.Usage.Page == USAGE_PAGE_BUTTON) ||
            (item->Attributes.Usage.Page == USAGE_PAGE_GENERIC_DCTRL));
  }
  return false;
}

//--------------------------------------------------------------------+
// Public API Implementation
//--------------------------------------------------------------------+

bool tuh_hid_is_mounted(uint8_t dev_addr) {
  hidh_device_t* dev = find_device(dev_addr);
  return dev && dev->mounted && tuh_hid_mounted(dev->dev_addr, dev->instance);
}

HID_TYPE tuh_hid_get_type(uint8_t dev_addr) {
  hidh_device_t* dev = find_device(dev_addr);
  return dev ? dev->hid_type : HID_UNDEFINED;
}

bool tuh_hid_is_busy(uint8_t dev_addr) {
  hidh_device_t* dev = find_device(dev_addr);
  if (!dev) return false;
  return dev->report_pending;
}

bool hid_app_request_report(uint8_t dev_addr, void * p_report) {
  hidh_device_t* dev = find_device(dev_addr);
  if (!dev) return false;
  
  // In TinyUSB, reports are automatically received via callbacks
  // Just store the destination buffer and mark as pending
  dev->report_dest = p_report;
  dev->report_pending = true;
  
  return true;
}

uint16_t tuh_hid_get_report_size(uint8_t dev_addr) {
  hidh_device_t* dev = find_device(dev_addr);
  return dev ? dev->report_size : 0;
}

HID_ReportInfo_t* tuh_hid_get_report_info(uint8_t dev_addr) {
  hidh_device_t* dev = find_device(dev_addr);
  return dev ? &dev->report_info : NULL;
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when device with HID interface is mounted
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_desc, uint16_t desc_len) {
  debug_mount_calls++;
  debug_last_dev_addr = dev_addr;
  debug_last_instance = instance;
  
  // Check for game controllers (VID/PID detection)
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  uint8_t protocol = tuh_hid_interface_protocol(dev_addr, instance);
  
  // Debug disabled for performance - enable for troubleshooting
  #if 0
  extern ssd1306_t disp;
  
  ssd1306_clear(&disp);
  ssd1306_draw_string(&disp, 5, 0, 2, (char*)"HID!!");
  
  char line1[20];
  snprintf(line1, sizeof(line1), "Addr:%d Inst:%d", dev_addr, instance);
  ssd1306_draw_string(&disp, 5, 25, 1, line1);
  
  char line2[20];
  snprintf(line2, sizeof(line2), "VID:%04X PID:%04X", vid, pid);
  ssd1306_draw_string(&disp, 5, 40, 1, line2);
  
  char line3[20];
  snprintf(line3, sizeof(line3), "P:%d L:%d", protocol, desc_len);
  ssd1306_draw_string(&disp, 5, 55, 1, line3);
  
  ssd1306_show(&disp);
  sleep_ms(3000);
  #endif
  
  // Check for GameCube USB Adapter
  bool is_gamecube = gc_is_adapter(vid, pid);
  
  // DEBUG: Console logging (always enabled for troubleshooting)
  printf("GC Check: VID=0x%04X, PID=0x%04X, is_gamecube=%d\n", vid, pid, is_gamecube);
  
#if ENABLE_CONTROLLER_DEBUG
  // Show on OLED for debugging - only in debug builds
  // Only show Nintendo VID devices to avoid spam from other devices
  if (vid == 0x057E) {  // Nintendo VID
    extern ssd1306_t disp;
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 1, (char*)"GC VID Check");
    char line[32];
    snprintf(line, sizeof(line), "v%s", PROJECT_VERSION_STRING);
    ssd1306_draw_string(&disp, 0, 10, 1, line);
    snprintf(line, sizeof(line), "V:%04X P:%04X", vid, pid);
    ssd1306_draw_string(&disp, 0, 24, 1, line);
    snprintf(line, sizeof(line), "Match:%d Inst:%d", is_gamecube, instance);
    ssd1306_draw_string(&disp, 0, 36, 1, line);
    snprintf(line, sizeof(line), "Addr:%d Prot:%d", dev_addr, protocol);
    ssd1306_draw_string(&disp, 0, 48, 1, line);
    ssd1306_show(&disp);
    sleep_ms(2000);  // Reduced to 2 seconds
  }
#endif
  
  if (is_gamecube) {
    printf("GameCube USB Adapter detected via HID: VID=0x%04X, PID=0x%04X, Instance=%d, Protocol=%d\n", 
           vid, pid, instance, protocol);
    
    // v11.1.3: HID Hijacking approach - let HID claim it, we handle the non-standard protocol
    // The adapter uses raw 37-byte reports with 0x21 signal byte
    
    extern ssd1306_t disp;
    char dbg[32];
    
#if ENABLE_CONTROLLER_DEBUG
    // Show protocol info (debug mode only)
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 1, (char*)"GC HID Mount");
    snprintf(dbg, sizeof(dbg), "Inst:%d Prot:%d", instance, protocol);
    ssd1306_draw_string(&disp, 0, 12, 1, dbg);
    ssd1306_draw_string(&disp, 0, 24, 1, (char*)"Initializing...");
    ssd1306_show(&disp);
    sleep_ms(1500);
#endif
    
    // Allocate device slot
    hidh_device_t* dev = alloc_device(dev_addr, instance);
    if (!dev) {
      printf("GC: ERROR - Cannot allocate device\n");
      return;
    }
    
    // Mark as GameCube joystick
    dev->hid_type = HID_JOYSTICK;
    dev->report_size = 64;  // Buffer size (reports are 37 bytes)
    dev->has_report_info = false;  // We'll parse manually
    
    // Only show splash and send init on instance 0
    if (instance == 0) {
#if ENABLE_CONTROLLER_DEBUG
      // Show splash (debug mode only)
      ssd1306_clear(&disp);
      ssd1306_draw_string(&disp, 10, 10, 2, (char*)"GCube");
      ssd1306_draw_string(&disp, 5, 35, 1, (char*)"USB Adapter");
      ssd1306_show(&disp);
      sleep_ms(2000);
#endif
      
      // STEP 1: Control transfer for third-party adapter compatibility
      // Windows driver shows this is required: bmRequestType=0x21, bRequest=11, wValue=1, wIndex=0
      printf("GC: Sending control transfer (request 11, value 1)...\n");
      tusb_control_request_t ctrl_req = {
        .bmRequestType_bit = {
          .recipient = TUSB_REQ_RCPT_INTERFACE,
          .type = TUSB_REQ_TYPE_CLASS,
          .direction = TUSB_DIR_OUT
        },
        .bRequest = 11,
        .wValue = 1,
        .wIndex = 0,
        .wLength = 0
      };
      
      uint8_t ctrl_result = XFER_RESULT_INVALID;
      
      tuh_xfer_t ctrl_xfer = {
        .daddr = dev_addr,
        .ep_addr = 0,
        .setup = &ctrl_req,
        .buffer = NULL,
        .complete_cb = NULL,  // Synchronous - blocks until complete
        .user_data = (uintptr_t)&ctrl_result
      };
      
      bool ctrl_ok = tuh_control_xfer(&ctrl_xfer);
      printf("GC: Control transfer queued: %d\n", ctrl_ok);
      
      // Small delay for control transfer to complete
      sleep_ms(200);
      printf("GC: Control transfer result: %d\n", ctrl_result);
      
#if ENABLE_CONTROLLER_DEBUG
      ssd1306_clear(&disp);
      ssd1306_draw_string(&disp, 0, 0, 1, (char*)"CTRL XFER");
      snprintf(dbg, sizeof(dbg), "Req:11 Val:1");
      ssd1306_draw_string(&disp, 0, 12, 1, dbg);
      snprintf(dbg, sizeof(dbg), "Result:%d", ctrl_result);
      ssd1306_draw_string(&disp, 0, 24, 1, dbg);
      ssd1306_show(&disp);
      sleep_ms(1500);
#endif
      
      // STEP 2: Send 0x13 init command via interrupt OUT endpoint
      // All reference drivers do this after control transfer
      printf("GC: Sending 0x13 init to interrupt OUT endpoint...\n");
      static const uint8_t gc_init = 0x13;
      
      // Use tuh_hid_send_report() to send to interrupt OUT endpoint 0x02
      bool send_ok = tuh_hid_send_report(dev_addr, instance, 0, &gc_init, 1);
      
      if (send_ok) {
        printf("GC: Init 0x13 queued to endpoint 0x02\n");
      } else {
        printf("GC: WARNING - Init 0x13 queue failed!\n");
      }
      
#if ENABLE_CONTROLLER_DEBUG
      // Show status (debug mode only)
      ssd1306_clear(&disp);
      ssd1306_draw_string(&disp, 0, 0, 1, (char*)"GC Init 0x13");
      snprintf(dbg, sizeof(dbg), "Addr:%d Inst:%d", dev_addr, instance);
      ssd1306_draw_string(&disp, 0, 12, 1, dbg);
      ssd1306_draw_string(&disp, 0, 24, 1, (char*)"PC mode?");
      ssd1306_draw_string(&disp, 0, 36, 1, (char*)"Ctrl plugged?");
      ssd1306_draw_string(&disp, 0, 48, 1, (char*)"Waiting...");
      ssd1306_show(&disp);
      sleep_ms(3000);
#endif
      
      // Notify application layer
      extern void gc_notify_mount(uint8_t dev_addr);
      gc_notify_mount(dev_addr);
      
      // Call mounted callback
      tuh_hid_mounted_cb(dev_addr);
    } else {
      printf("GC: Additional interface %d registered\n", instance);
      
      // Send init to additional instances too (might be needed for PC mode)
      printf("GC: Sending control transfer to instance %d...\n", instance);
      tusb_control_request_t ctrl_req = {
        .bmRequestType_bit = {
          .recipient = TUSB_REQ_RCPT_INTERFACE,
          .type = TUSB_REQ_TYPE_CLASS,
          .direction = TUSB_DIR_OUT
        },
        .bRequest = 11,
        .wValue = 1,
        .wIndex = instance,  // Use instance number for wIndex
        .wLength = 0
      };
      
      uint8_t ctrl_result = XFER_RESULT_INVALID;
      tuh_xfer_t ctrl_xfer = {
        .daddr = dev_addr,
        .ep_addr = 0,
        .setup = &ctrl_req,
        .buffer = NULL,
        .complete_cb = NULL,
        .user_data = (uintptr_t)&ctrl_result
      };
      
      tuh_control_xfer(&ctrl_xfer);
      sleep_ms(50);
      
      // Send 0x13 to this instance too
      static const uint8_t gc_init = 0x13;
      tuh_hid_send_report(dev_addr, instance, 0, &gc_init, 1);
      
      // Also notify for additional instances (causes joystick counter++)
      extern void gc_notify_mount(uint8_t dev_addr);
      gc_notify_mount(dev_addr);
    }
    
    // Start receiving reports
    printf("GC: Calling tuh_hid_receive_report(addr=%d, inst=%d)...\n", dev_addr, instance);
    
#if ENABLE_CONTROLLER_DEBUG
    // Show we're starting report reception (debug mode only)
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 2, (char*)"RCV START");
    snprintf(dbg, sizeof(dbg), "A:%d I:%d", dev_addr, instance);
    ssd1306_draw_string(&disp, 0, 20, 1, dbg);
    ssd1306_draw_string(&disp, 0, 32, 1, (char*)"Queueing...");
    ssd1306_show(&disp);
    sleep_ms(1000);
#endif
    
    bool recv_ok = tuh_hid_receive_report(dev_addr, instance);
    printf("GC: tuh_hid_receive_report result: %d\n", recv_ok);
    
#if ENABLE_CONTROLLER_DEBUG
    // Show status (debug mode only)
    ssd1306_clear(&disp);
    if (recv_ok) {
      ssd1306_draw_string(&disp, 0, 0, 2, (char*)"RCV OK!");
      ssd1306_draw_string(&disp, 0, 20, 1, (char*)"Queued");
      ssd1306_draw_string(&disp, 0, 32, 1, (char*)"Waiting for");
      ssd1306_draw_string(&disp, 0, 44, 1, (char*)"controller...");
    } else {
      ssd1306_draw_string(&disp, 0, 0, 2, (char*)"RCV FAIL!");
      snprintf(dbg, sizeof(dbg), "A:%d I:%d", dev_addr, instance);
      ssd1306_draw_string(&disp, 0, 20, 1, dbg);
      ssd1306_draw_string(&disp, 0, 32, 1, (char*)"Can't start");
      ssd1306_draw_string(&disp, 0, 44, 1, (char*)"reports!");
    }
    ssd1306_show(&disp);
    sleep_ms(recv_ok ? 2000 : 5000);
#else
  #if ENABLE_OLED_DISPLAY
    // Production splash screen - show GameCube adapter detected (matching Xbox/PS style)
    if (instance == 0) {  // Only show once for instance 0
      ssd1306_clear(&disp);
      ssd1306_draw_string(&disp, 0, 10, 2, (char*)"GAMECUBE!");
      ssd1306_draw_string(&disp, 5, 35, 1, (char*)"USB Adapter");
      ssd1306_show(&disp);
      sleep_ms(2000);
    }
  #endif
#endif
    
    return;
  }
  

  // Check for PS3 DualShock 3
  bool is_ps3 = ps3_is_dualshock3(vid, pid);
  
  if (is_ps3) {
    printf("PS3 DualShock 3 detected: VID=0x%04X, PID=0x%04X\n", vid, pid);
    
    // Allocate device slot
    hidh_device_t* dev = alloc_device(dev_addr, instance);
    if (!dev) return;
    
    // Mark as PS3 joystick
    dev->hid_type = HID_JOYSTICK;
    dev->report_size = 64;  // PS3 reports can be large
    dev->has_report_info = false;  // We'll parse manually, not via HID parser
    
    // Notify PS3 module
    ps3_mount_cb(dev_addr);
    
    // Start receiving reports
    tuh_hid_receive_report(dev_addr, instance);
    
    // Call mounted callback
    tuh_hid_mounted_cb(dev_addr);
    return;
  }
  
  // Check for PS4 DualShock 4
  bool is_ps4 = ps4_is_dualshock4(vid, pid);
  
  if (is_ps4) {
    printf("PS4 DualShock 4 detected: VID=0x%04X, PID=0x%04X\n", vid, pid);
    
    // Allocate device slot
    hidh_device_t* dev = alloc_device(dev_addr, instance);
    if (!dev) return;
    
    // Mark as PS4 joystick
    dev->hid_type = HID_JOYSTICK;
    dev->report_size = 64;  // PS4 reports vary but we'll handle up to 64 bytes
    dev->has_report_info = false;  // We'll parse manually, not via HID parser
    
    // Notify PS4 module
    ps4_mount_cb(dev_addr);
    
    // Start receiving reports
    tuh_hid_receive_report(dev_addr, instance);
    
    // Call mounted callback
    tuh_hid_mounted_cb(dev_addr);
    return;
  }
  
  // Check for Google Stadia Controller
  // Stadia uses STANDARD HID, so we'll let it fall through to HID parser
  // We'll show splash screen AFTER device is allocated and parsed
  bool is_stadia = stadia_is_controller(vid, pid);
  
  if (is_stadia) {
    printf("Google Stadia controller detected: VID=0x%04X, PID=0x%04X\n", vid, pid);
    
    // Debug disabled for production - enable with ENABLE_STADIA_DEBUG
    #if ENABLE_STADIA_DEBUG
    extern ssd1306_t disp;
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 5, 0, 1, (char*)"STADIA DETECTED");
    char line[20];
    snprintf(line, sizeof(line), "Addr:%d Inst:%d", dev_addr, instance);
    ssd1306_draw_string(&disp, 5, 12, 1, line);
    snprintf(line, sizeof(line), "P:%d L:%d", protocol, desc_len);
    ssd1306_draw_string(&disp, 5, 24, 1, line);
    ssd1306_show(&disp);
    sleep_ms(1500);
    #endif
    
    // DON'T return - let it fall through to use HID parser below
    // This will parse the descriptor properly and get button info
    // Splash screen will be shown after successful parsing
  }
  
  // Check for Nintendo Switch controllers BEFORE generic HID parsing
  // This prevents Switch controllers from being detected as mice/keyboards
  bool is_switch = switch_is_controller(vid, pid);
  
  printf("HID Device detected: VID=0x%04X, PID=0x%04X, Protocol=%d, is_switch=%d\n", 
         vid, pid, protocol, is_switch);
  
  // Debug disabled - was used to verify PowerA detection
  #if 0
  if (vid == POWERA_VENDOR_ID) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 5, 0, 2, (char*)(is_switch ? "SWITCH!" : "NOT SW"));
    char line[20];
    snprintf(line, sizeof(line), "is_switch=%d", is_switch);
    ssd1306_draw_string(&disp, 5, 35, 1, line);
    snprintf(line, sizeof(line), "PID:%04X", pid);
    ssd1306_draw_string(&disp, 5, 50, 1, line);
    ssd1306_show(&disp);
    sleep_ms(3000);
  }
  #endif
  
  if (is_switch) {
    printf("Nintendo Switch controller detected: VID=0x%04X, PID=0x%04X, Protocol=%d\n", 
           vid, pid, protocol);
    
    // Allocate device slot
    hidh_device_t* dev = alloc_device(dev_addr, instance);
    if (!dev) return;
    
    // Mark as Switch joystick
    dev->hid_type = HID_JOYSTICK;
    dev->report_size = 64;  // Switch reports can vary
    dev->has_report_info = false;  // We'll parse manually
    
    // Notify Switch module
    switch_mount_cb(dev_addr);
    
    // Start receiving reports
    tuh_hid_receive_report(dev_addr, instance);
    
    // Call mounted callback
    tuh_hid_mounted_cb(dev_addr);
    return;
  }
  
  printf("Not a known controller: VID=0x%04X, PID=0x%04X, proceeding with HID parser\n", vid, pid);
  
  // Xbox controller detection removed - now handled by official xinput_host driver
  // The driver registers via usbh_app_driver_get_cb() and handles Xbox controllers directly
  
  hidh_device_t* dev = alloc_device(dev_addr, instance);
  if (!dev) return;
  
  // Count active devices
  debug_active_devices = 0;
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (hid_devices[i].mounted) debug_active_devices++;
  }
  
  // Check if it's a keyboard (boot protocol)
  if (protocol == HID_ITF_PROTOCOL_KEYBOARD) {
    dev->hid_type = HID_KEYBOARD;
    dev->report_size = sizeof(hid_keyboard_report_t);
    
    // Start receiving reports - CRITICAL for TinyUSB 0.12+
    tuh_hid_receive_report(dev_addr, instance);
    
    // Only call app callback for first interface of this device address
    // Find if we already have another interface for this dev_addr
    bool first_interface = true;
    for (int i = 0; i < CFG_TUH_HID; i++) {
      if (hid_devices[i].dev_addr == dev_addr && 
          hid_devices[i].mounted && 
          &hid_devices[i] != dev) {
        first_interface = false;
        break;
      }
    }
    
    if (first_interface) {
      tuh_hid_mounted_cb(dev_addr);
    }
  }
  // Check if it's a mouse (boot protocol) - but mice need report parsing
  else if (protocol == HID_ITF_PROTOCOL_MOUSE) {
    // Even boot protocol mice need the report parser for proper handling
    if (report_desc && desc_len > 0 && desc_len < 512) {
      filter_type = HID_UNDEFINED;
      if (USB_ProcessHIDReport(report_desc, desc_len, &dev->report_info) == HID_PARSE_Successful) {
        dev->has_report_info = true;
        dev->hid_type = HID_MOUSE;  // Force to MOUSE since we know the protocol
        dev->report_size = 64;
      }
    }
    
    // Fallback if parsing fails
    if (dev->hid_type == HID_UNDEFINED) {
      dev->hid_type = HID_MOUSE;
      dev->report_size = sizeof(hid_mouse_report_t);
    }
    
    // Start receiving reports - CRITICAL for TinyUSB 0.12+
    tuh_hid_receive_report(dev_addr, instance);
    
    // Always call mounted callback for boot protocol mice
    // (Logitech Unifying has mouse on non-zero instance)
    // Use marker bit to distinguish from keyboard on same address
    tuh_hid_mounted_cb(dev_addr | 0x80);
  }
  // For other devices (joysticks, non-boot mice), try to parse descriptor
  else if (report_desc && desc_len > 0 && desc_len < 512) {
    filter_type = HID_UNDEFINED;
    bool parse_success = (USB_ProcessHIDReport(report_desc, desc_len, &dev->report_info) == HID_PARSE_Successful);
    
    if (parse_success) {
      dev->has_report_info = true;
      dev->hid_type = filter_type;
      dev->report_size = 64;
    } else {
      // Parser failed - set defaults
      dev->has_report_info = false;
      dev->report_size = 64;
    }
    
    // Debug: Show what we detected
    const char* type_str = (filter_type == HID_MOUSE) ? "MOUSE" : 
                           (filter_type == HID_JOYSTICK) ? "JOYSTICK" : 
                           (filter_type == HID_KEYBOARD) ? "KEYBOARD" : "UNKNOWN";
    printf("HID Parser detected: %s (dev_addr=%d, inst=%d, parse_success=%d)\n", 
           type_str, dev_addr, instance, parse_success);
    
    // Stadia controller: Force to JOYSTICK (splash screen shown in C++ layer)
    if (is_stadia) {
      printf("Stadia: HID parser result = %s, forcing to JOYSTICK\n", type_str);
      printf("Stadia: parse_success=%d, desc_len=%d, filter_type before=%d\n", 
             parse_success, desc_len, filter_type);
      dev->hid_type = HID_JOYSTICK;
      filter_type = HID_JOYSTICK;
      printf("Stadia: Set filter_type to JOYSTICK (%d)\n", filter_type);
    }
    
    // Debug disabled for performance
    #if 0
    if (vid == 0x046D) {
      extern ssd1306_t disp;
      
      ssd1306_clear(&disp);
      ssd1306_draw_string(&disp, 20, 10, 2, (char*)"PARSED");
      
      char type_line[20];
      snprintf(type_line, sizeof(type_line), "Type: %s", type_str);
      ssd1306_draw_string(&disp, 5, 35, 1, type_line);
      
      char items_line[20];
      snprintf(items_line, sizeof(items_line), "Items: %d", dev->report_info.TotalReportItems);
      ssd1306_draw_string(&disp, 5, 50, 1, items_line);
      
      ssd1306_show(&disp);
      sleep_ms(2000);
    }
    #endif
    
    // Start receiving reports
    tuh_hid_receive_report(dev_addr, instance);
    
    // Call mounted callback
    // For keyboards, call normally
    if (filter_type == HID_KEYBOARD) {
      tuh_hid_mounted_cb(dev_addr);
    }
    // For mice on multi-interface devices, call with a special marker
    // so C++ layer knows it's the mouse interface
    else if (filter_type == HID_MOUSE) {
      // Mark mouse with high bit set (128-255 range)
      // This allows C++ layer to distinguish mouse from keyboard on same address
      tuh_hid_mounted_cb(dev_addr | 0x80);
    }
    // For other device types (joysticks including Stadia), call mounted callback
    else {
      // For Stadia, always call (no interface check needed)
      if (is_stadia) {
        printf("Stadia: Calling tuh_hid_mounted_cb(dev_addr=%d)\n", dev_addr);
        
        // Debug disabled for production - enable with ENABLE_STADIA_DEBUG
        #if ENABLE_STADIA_DEBUG
        extern ssd1306_t disp;
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 5, 0, 1, (char*)"STADIA CALLBACK");
        char line[20];
        snprintf(line, sizeof(line), "filter_type=%d", filter_type);
        ssd1306_draw_string(&disp, 5, 15, 1, line);
        ssd1306_show(&disp);
        sleep_ms(1000);
        #endif
        
        tuh_hid_mounted_cb(dev_addr);
      } else {
        // For other joysticks, only call for first interface
        bool first_interface = true;
        for (int i = 0; i < CFG_TUSB_HOST_DEVICE_MAX; i++) {
          if (hid_devices[i].dev_addr == dev_addr && 
              hid_devices[i].mounted && 
              &hid_devices[i] != dev) {
            first_interface = false;
            break;
          }
        }
        
        if (first_interface) {
          tuh_hid_mounted_cb(dev_addr);
        }
      }
    }
  }
  // If no descriptor or parsing completely failed, but it's Stadia, still register it
  else if (is_stadia) {
    printf("Stadia: No descriptor or parsing failed - fallback path\n");
    printf("Stadia: report_desc=%p, desc_len=%d\n", report_desc, desc_len);
    dev->hid_type = HID_JOYSTICK;
    dev->report_size = 64;
    tuh_hid_receive_report(dev_addr, instance);
    // Splash screen shown in C++ layer (tuh_hid_mounted_cb)
    printf("Stadia: Fallback - Calling tuh_hid_mounted_cb(dev_addr=%d)\n", dev_addr);
    tuh_hid_mounted_cb(dev_addr);
  }
}

// Invoked when device with HID interface is unmounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  debug_unmount_calls++;
  
  hidh_device_t* dev = find_device_by_inst(dev_addr, instance);
  if (!dev) return;
  
  // FIX: Check if this is a PS4 or Switch controller and call unmount callback
  // (Stadia now uses generic HID path, no special unmount needed)
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  if (gc_is_adapter(vid, pid)) {
    gc_unmount_cb(dev_addr);
  } else if (ps3_is_dualshock3(vid, pid)) {
    ps3_unmount_cb(dev_addr);
  } else if (ps4_is_dualshock4(vid, pid)) {
    ps4_unmount_cb(dev_addr);
  } else if (switch_is_controller(vid, pid)) {
    switch_unmount_cb(dev_addr);
  }
  
  // Clear report destination to prevent callbacks to freed memory
  dev->report_dest = NULL;
  dev->report_pending = false;
  
  // Only call app unmount callback once per device (for first instance)
  bool should_notify = true;
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (hid_devices[i].dev_addr == dev_addr && 
        hid_devices[i].mounted && 
        hid_devices[i].instance < instance) {
      should_notify = false;
      break;
    }
  }
  
  if (should_notify) {
    tuh_hid_unmounted_cb(dev_addr);
  }
  
  // Update UI with current counts (including Xbox controllers)
  extern void xinput_notify_ui_unmount();
  xinput_notify_ui_unmount();
  
  // Clear this device slot
  memset(dev, 0, sizeof(hidh_device_t));
}

// Invoked when received report from device via interrupt endpoint
// In TinyUSB 0.12+, this is called when reports arrive
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  debug_report_calls++;
  
  // DEBUG: Minimal GameCube report tracking (disabled excessive screens)
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  if (vid == 0x057E && pid == 0x0337) {
    static uint32_t gc_raw_count = 0;
    gc_raw_count++;
    
    // Only log to console, no OLED spam
    if ((gc_raw_count % 100) == 1) {
      printf("GC: Report #%lu addr=%d inst=%d len=%d\n", gc_raw_count, dev_addr, instance, len);
    }
  }
  
  // Safety checks
  if (!report || len == 0 || len > 64) return;
  
  hidh_device_t* dev = find_device_by_inst(dev_addr, instance);
  
  if (!dev || !dev->mounted) return;
  
  // DEBUG: Minimal GameCube report tracking (console only, no OLED spam)
  static uint32_t gc_vid_count = 0;
  if (vid == 0x057E && pid == 0x0337) {
    gc_vid_count++;
    
    // Console logging only
    if ((gc_vid_count % 100) == 1) {
      printf("GC: Report callback #%lu addr=%d inst=%d len=%d\n", gc_vid_count, dev_addr, instance, len);
    }
  }

  // Stadia: If reports arrive before the normal mount flow triggers callback,
  // ensure the application is notified immediately on first report.
  if (stadia_is_controller(vid, pid) && !stadia_notified[dev_addr]) {
    // Force classify as joystick for app layer and notify
    hidh_device_t* d = find_device_by_inst(dev_addr, instance);
    if (d) {
      d->hid_type = HID_JOYSTICK;
    }
    
    #if ENABLE_STADIA_DEBUG
    // OLED hint for troubleshooting
    extern ssd1306_t disp;
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 1, (char*)"STADIA FIRST RPT");
    char line[20];
    snprintf(line, sizeof(line), "A:%d I:%d L:%d", dev_addr, instance, len);
    ssd1306_draw_string(&disp, 0, 12, 1, line);
    ssd1306_show(&disp);
    sleep_ms(500);
    #endif

    tuh_hid_mounted_cb(dev_addr);
    stadia_notified[dev_addr] = true;
  }
  
  // Debug disabled for performance
  #if 0
  if (vid == 0x046D) {
    static uint32_t logitech_report_count = 0;
    logitech_report_count++;
    
    if ((logitech_report_count % 100) == 0) {
      extern ssd1306_t disp;
      
      ssd1306_clear(&disp);
      ssd1306_draw_string(&disp, 15, 0, 1, (char*)"LOGITECH DATA");
      
      char type_line[20];
      const char* type = (dev->hid_type == HID_MOUSE) ? "Mouse" :
                         (dev->hid_type == HID_KEYBOARD) ? "Keyboard" :
                         (dev->hid_type == HID_JOYSTICK) ? "Joystick" : "Unknown";
      snprintf(type_line, sizeof(type_line), "Type: %s", type);
      ssd1306_draw_string(&disp, 5, 15, 1, type_line);
      
      char count_line[20];
      snprintf(count_line, sizeof(count_line), "Reports: %lu", logitech_report_count);
      ssd1306_draw_string(&disp, 5, 30, 1, count_line);
      
      char data_line[20];
      snprintf(data_line, sizeof(data_line), "Len:%d A:%d I:%d", 
               len, dev_addr, instance);
      ssd1306_draw_string(&disp, 5, 45, 1, data_line);
      
      ssd1306_show(&disp);
    }
  }
  #endif
  
  // GameCube USB Adapter
  if (gc_is_adapter(vid, pid)) {
    // This is a GameCube adapter report - pass to GC handler
    static uint32_t gc_callback_count = 0;
    gc_callback_count++;
    
    if ((gc_callback_count % 100) == 1) {
      printf("GC: Report callback #%lu, len=%d\n", gc_callback_count, len);
    }
    
    gc_process_report(dev_addr, report, len);
    
    // Queue next report
    tuh_hid_receive_report(dev_addr, instance);
    return;
  }


  // PS3 DualShock 3
  if (ps3_is_dualshock3(vid, pid)) {
    // This is a PS3 controller report - pass to PS3 handler
    ps3_process_report(dev_addr, report, len);
    
    // Queue next report
    tuh_hid_receive_report(dev_addr, instance);
    return;
  }

  // PS4 DualShock 4
  if (ps4_is_dualshock4(vid, pid)) {
    // This is a PS4 controller report - pass to PS4 handler
    ps4_process_report(dev_addr, report, len);
    
    // Queue next report
    tuh_hid_receive_report(dev_addr, instance);
    return;
  }
  
  // Nintendo Switch controllers
  if (switch_is_controller(vid, pid)) {
    // This is a Switch controller report - pass to Switch handler
    switch_process_report(dev_addr, report, len);
    
    // Queue next report
    tuh_hid_receive_report(dev_addr, instance);
    return;
  }
  
  // Google Stadia controllers - now using standard HID parsing
  // Report processing removed - handled by generic HID path below
  
  // Xbox controllers now handled by official xinput_host driver
  // Reports go directly to tuh_xinput_report_received_cb()
  
  // Always store the latest report in our buffer
  uint16_t copy_len = (len < 64) ? len : 64;
  memcpy(dev->report_buffer, report, copy_len);
  
  // If application has provided a destination buffer, copy the report there
  if (dev->report_dest && dev->report_pending) {
    debug_report_copied++;
    
    memcpy(dev->report_dest, report, copy_len);
    dev->report_pending = false;
    
    // Call the application's ISR callback
    extern void tuh_hid_isr(uint8_t dev_addr, xfer_result_t event);
    tuh_hid_isr(dev_addr, XFER_RESULT_SUCCESS);
    
    // Clear the dest pointer after processing
    dev->report_dest = NULL;
  }
  
  // Queue next report - CRITICAL for continuous operation in TinyUSB 0.12+
  tuh_hid_receive_report(dev_addr, instance);
}

//--------------------------------------------------------------------+
// Xbox Controller Detection (via Device Descriptor)
//--------------------------------------------------------------------+
// Note: TinyUSB 0.19.0 doesn't fully support vendor class callbacks
// We detect Xbox controllers via VID/PID in the mount callback above
