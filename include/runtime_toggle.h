/*
 * Runtime USB/Bluetooth Toggle Control
 * Allows enabling/disabling USB and Bluetooth at runtime without rebuild
 */

#ifndef RUNTIME_TOGGLE_H
#define RUNTIME_TOGGLE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Runtime control functions
// These can be called to enable/disable USB or Bluetooth at runtime
void usb_runtime_enable(void);
void usb_runtime_disable(void);
bool usb_runtime_is_enabled(void);

#if ENABLE_BLUEPAD32
void bt_runtime_enable(void);
void bt_runtime_disable(void);
bool bt_runtime_is_enabled(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // RUNTIME_TOGGLE_H

