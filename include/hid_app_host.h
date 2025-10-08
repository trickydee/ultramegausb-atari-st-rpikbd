/* 
 * HID Application Host - Wrapper providing high-level HID API
 * This provides the same API as the custom TinyUSB fork but works with official TinyUSB
 */

#ifndef _HID_APP_HOST_H_
#define _HID_APP_HOST_H_

#include "tusb.h"
#include "HIDParser.h"

/** HID Report Descriptor Usage Page value for a Generic Desktop Control. */
#define USAGE_PAGE_GENERIC_DCTRL    0x01
#define USAGE_MOUSE                 0x02
#define USAGE_JOYSTICK              0x04
#define USAGE_X                     0x30
#define USAGE_Y                     0x31
#define USAGE_PAGE_BUTTON           0x09

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HID_UNDEFINED,
  HID_KEYBOARD,
  HID_MOUSE,
  HID_JOYSTICK
} HID_TYPE;

// Check if the device is a HID device
bool tuh_hid_is_mounted(uint8_t dev_addr);

// Get the type of the HID device if known
HID_TYPE tuh_hid_get_type(uint8_t dev_addr);

// Check if the interface is currently busy or not
bool tuh_hid_is_busy(uint8_t dev_addr);

// Perform a get report from HID interface
bool tuh_hid_get_report(uint8_t dev_addr, void * p_report);

// Get the size of the HID report in bytes
uint16_t tuh_hid_get_report_size(uint8_t dev_addr);

// Get the report info structure used for parsing a report
HID_ReportInfo_t* tuh_hid_get_report_info(uint8_t dev_addr);

// Application callbacks
void tuh_hid_mounted_cb(uint8_t dev_addr);
void tuh_hid_unmounted_cb(uint8_t dev_addr);

// Debug functions
uint32_t hid_debug_get_mount_calls(void);
uint32_t hid_debug_get_report_calls(void);
uint32_t hid_debug_get_report_copied(void);
uint32_t hid_debug_get_unmount_calls(void);

#ifdef __cplusplus
}
#endif

#endif /* _HID_APP_HOST_H_ */
