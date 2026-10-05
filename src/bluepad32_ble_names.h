#ifndef BLUEPAD32_BLE_NAMES_H
#define BLUEPAD32_BLE_NAMES_H
#include <uni_hid_device.h>
void ultrausbt_restore_ble_name(uni_hid_device_t *device);
void ultrausbt_save_ble_name(uni_hid_device_t *device);
void ultrausbt_clear_ble_names(void);
#endif
