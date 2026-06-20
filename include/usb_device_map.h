/*
 * USB device names for the Map Devices OLED screen.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define USB_MAP_NAME_LEN 24

#ifdef __cplusplus
extern "C" {
#endif

void usb_map_register_gamepad(uint8_t dev_addr, const char* name);
void usb_map_unregister_gamepad(uint8_t dev_addr);
bool usb_map_gamepad_registered(uint8_t dev_addr);
const char* usb_map_get_gamepad(int slot);

void usb_map_set_keyboard(const char* name);
void usb_map_clear_keyboard(void);
const char* usb_map_get_keyboard(void);

void usb_map_set_mouse(const char* name);
void usb_map_clear_mouse(void);
const char* usb_map_get_mouse(void);

#ifdef __cplusplus
}
#endif
