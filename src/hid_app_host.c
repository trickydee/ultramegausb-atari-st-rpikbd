/* 
 * HID Application Host - Adapter for official TinyUSB
 * This provides backward compatibility with the custom TinyUSB fork API
 */

#include "tusb.h"
#include "hid_app_host.h"
#include "xinput.h"
#include "ps4_controller.h"
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
  snprintf(line2, sizeof(line2), "VID:%04X", vid);
  ssd1306_draw_string(&disp, 5, 40, 1, line2);
  
  char line3[20];
  snprintf(line3, sizeof(line3), "P:%d L:%d", protocol, desc_len);
  ssd1306_draw_string(&disp, 5, 55, 1, line3);
  
  ssd1306_show(&disp);
  sleep_ms(3000);
  #endif
  
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
  
  // Check for Xbox controller
  bool is_xbox = xinput_is_xbox_controller(vid, pid);
  
  if (is_xbox) {
    printf("Xbox controller detected: VID=0x%04X, PID=0x%04X\n", vid, pid);
    printf("Xbox: Will be treated as raw HID joystick (vendor-class workaround)\n");
    
    // Allocate device slot
    hidh_device_t* dev = alloc_device(dev_addr, instance);
    if (!dev) return;
    
    // Mark as Xbox type joystick
    dev->hid_type = HID_JOYSTICK;
    dev->report_size = 64;
    dev->has_report_info = false;
    
    // Notify Xbox module
    xinput_mount_cb(dev_addr);
    
    // Start receiving reports
    tuh_hid_receive_report(dev_addr, instance);
    
    // Call mounted callback
    tuh_hid_mounted_cb(dev_addr);
    return;
  }
  
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
    if (USB_ProcessHIDReport(report_desc, desc_len, &dev->report_info) == HID_PARSE_Successful) {
      dev->has_report_info = true;
      dev->hid_type = filter_type;
      dev->report_size = 64;
      
      // Debug: Show what we detected
      const char* type_str = (filter_type == HID_MOUSE) ? "MOUSE" : 
                             (filter_type == HID_JOYSTICK) ? "JOYSTICK" : "UNKNOWN";
      printf("HID Parser detected: %s (dev_addr=%d, inst=%d)\n", type_str, dev_addr, instance);
      
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
      
      // Debug disabled for performance
      #if 0
      if (vid == 0x046D) {
        extern ssd1306_t disp;
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 10, 0, 1, (char*)"CALLING CB");
        
        char line1[20];
        const char* ft = (filter_type == HID_MOUSE) ? "MOUSE" : 
                         (filter_type == HID_KEYBOARD) ? "KEYBOARD" : "OTHER";
        snprintf(line1, sizeof(line1), "Type: %s", ft);
        ssd1306_draw_string(&disp, 5, 20, 1, line1);
        
        char line2[20];
        uint8_t cb_addr = (filter_type == HID_MOUSE) ? (dev_addr | 0x80) : dev_addr;
        snprintf(line2, sizeof(line2), "Addr: %d", cb_addr);
        ssd1306_draw_string(&disp, 5, 35, 1, line2);
        
        ssd1306_show(&disp);
        sleep_ms(2000);
      }
      #endif
      
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
      // For other device types (joysticks), only call for first interface
      else {
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
}

// Invoked when device with HID interface is unmounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  debug_unmount_calls++;
  
  hidh_device_t* dev = find_device_by_inst(dev_addr, instance);
  if (!dev) return;
  
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
  
  // Clear this device slot
  memset(dev, 0, sizeof(hidh_device_t));
}

// Invoked when received report from device via interrupt endpoint
// In TinyUSB 0.12+, this is called when reports arrive
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  debug_report_calls++;
  
  // Safety checks
  if (!report || len == 0 || len > 64) return;
  
  hidh_device_t* dev = find_device_by_inst(dev_addr, instance);
  if (!dev || !dev->mounted) return;
  
  // Check if this is a game controller report
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  
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
  
  // PS4 DualShock 4
  if (ps4_is_dualshock4(vid, pid)) {
    // This is a PS4 controller report - pass to PS4 handler
    ps4_process_report(dev_addr, report, len);
    
    // Queue next report
    tuh_hid_receive_report(dev_addr, instance);
    return;
  }
  
  // Xbox controllers
  if (xinput_is_xbox_controller(vid, pid)) {
    // This is an Xbox controller report - pass to XInput handler
    xinput_process_report(dev_addr, report, len);
    
    // Queue next report
    tuh_hid_receive_report(dev_addr, instance);
    return;
  }
  
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
