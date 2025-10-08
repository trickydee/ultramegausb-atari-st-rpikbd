/* 
 * HID Application Host - Adapter for official TinyUSB
 * This provides backward compatibility with the custom TinyUSB fork API
 */

#include "tusb.h"
#include "hid_app_host.h"
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

// Find device by address
static hidh_device_t* find_device(uint8_t dev_addr) {
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (hid_devices[i].dev_addr == dev_addr && hid_devices[i].mounted) {
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
  
  hidh_device_t* dev = alloc_device(dev_addr, instance);
  if (!dev) return;
  
  // Count active devices
  debug_active_devices = 0;
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (hid_devices[i].mounted) debug_active_devices++;
  }
  
  uint8_t protocol = tuh_hid_interface_protocol(dev_addr, instance);
  
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
    
    // Only call app callback for first interface of this device address
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
  // For other devices (joysticks), try to parse descriptor
  else if (report_desc && desc_len > 0 && desc_len < 512) {
    filter_type = HID_UNDEFINED;
    if (USB_ProcessHIDReport(report_desc, desc_len, &dev->report_info) == HID_PARSE_Successful) {
      dev->has_report_info = true;
      dev->hid_type = filter_type;
      dev->report_size = 64;
      
      // Start receiving reports
      tuh_hid_receive_report(dev_addr, instance);
      
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