// Copyright (c) 2026 ultrausbt. SPDX-License-Identifier: EPL-2.0
// Bluepad32 develop uses the BTstack 1.8 HIDS host API spelling.
// Keep the adapter SDK's tested BTstack; bridge its equivalent 1.6 client names.
#pragma once
#if !__has_include(<ble/gatt-service/hids_host.h>)
#define hids_host_connect hids_client_connect
#define hids_host_descriptor_storage_get_descriptor_data hids_client_descriptor_storage_get_descriptor_data
#define hids_host_descriptor_storage_get_descriptor_len hids_client_descriptor_storage_get_descriptor_len
#define hids_host_disconnect hids_client_disconnect
#define hids_host_init hids_client_init
#define hids_host_send_write_report hids_client_send_write_report
#endif
