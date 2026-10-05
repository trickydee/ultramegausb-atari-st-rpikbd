/* Copyright (c) 2026 ultrausbt — https://github.com/trickydee/ultrausbt-amiga
 * SPDX-License-Identifier: EPL-2.0
 * Name persistence shared with the Amiga adapter; reconnect stays in Bluepad32.
 */
#include <btstack.h>
#include <ble/le_device_db.h>
#include <ble/gatt-service/device_information_service_client.h>
#include <btstack_tlv.h>
#include <stdio.h>
#include <string.h>
#include <uni_log.h>
#include "bluepad32_ble_names.h"

// The SDK has one Device Information client. Observe its existing query rather
// than starting another discovery or changing the shared Bluepad32 checkout.
static btstack_packet_handler_t device_information_handler;

static void ble_name_device_information_handler(uint8_t packet_type, uint16_t channel,
                                               uint8_t *packet, uint16_t size) {
    btstack_packet_handler_t handler = device_information_handler;
    if (packet_type == HCI_EVENT_PACKET && size >= 3 &&
        hci_event_packet_get_type(packet) == HCI_EVENT_GATTSERVICE_META) {
        uint8_t event = hci_event_gattservice_meta_get_subevent_code(packet);
        if (event == GATTSERVICE_SUBEVENT_DEVICE_INFORMATION_MODEL_NUMBER && size >= 7 &&
            gattservice_subevent_device_information_model_number_get_att_status(packet) == ATT_ERROR_SUCCESS) {
            uni_hid_device_t *device = uni_hid_device_get_instance_for_connection_handle(
                gattservice_subevent_device_information_model_number_get_con_handle(packet));
            if (device && (!device->name[0] || strncmp(device->name, "BLE ", 4) == 0)) {
                ultrausbt_restore_ble_name(device);
                if (!device->name[0] || strncmp(device->name, "BLE ", 4) == 0) {
                    const char *model = gattservice_subevent_device_information_model_number_get_value(packet);
                    if (model[0] && memchr(model, 0, size - 6))
                        uni_hid_device_set_name(device, model);
                }
            }
        } else if (event == GATTSERVICE_SUBEVENT_DEVICE_INFORMATION_DONE) {
            // Release before forwarding: the callback may start another query.
            device_information_handler = NULL;
        }
    }
    if (handler) handler(packet_type, channel, packet, size);
}

uint8_t __real_device_information_service_client_query(hci_con_handle_t con_handle,
                                                      btstack_packet_handler_t handler);

uint8_t __wrap_device_information_service_client_query(hci_con_handle_t con_handle,
                                                      btstack_packet_handler_t handler) {
    btstack_packet_handler_t previous = device_information_handler;
    device_information_handler = handler;
    uint8_t status = __real_device_information_service_client_query(
        con_handle, ble_name_device_information_handler);
    // A rejected duplicate must not replace the in-flight query's callback.
    if (status != ERROR_CODE_SUCCESS) device_information_handler = previous;
    return status;
}

// Display metadata in the existing BTstack flash store, keyed by bond slot.
// Verify the full identity on reads: a recycled bond slot must not inherit a name.
// UBN tags are private to ultrausbt; they contain no authentication material.
#define BLE_NAME_TAG_BASE 0x55424e00u
struct ble_name_record {
    uint8_t version, address_type;
    bd_addr_t address;
    char name[32];
};

static int ble_name_identity(uni_hid_device_t *device, struct ble_name_record *record) {
    int index = sm_le_device_index(device->conn.handle);
    if (index < 0 || index >= le_device_db_max_count() || index > 255) return -1;
    int type;
    le_device_db_info(index, &type, record->address, NULL);
    if (type != BD_ADDR_TYPE_LE_PUBLIC && type != BD_ADDR_TYPE_LE_RANDOM) return -1;
    record->version = 1;
    record->address_type = (uint8_t)type;
    return index;
}

void ultrausbt_restore_ble_name(uni_hid_device_t *device) {
    struct ble_name_record identity = {0}, saved = {0};
    int index = ble_name_identity(device, &identity);
    if (index < 0) return;
    const btstack_tlv_t *tlv;
    void *context;
    btstack_tlv_get_instance(&tlv, &context);
    if (!tlv) return;
    int size = tlv->get_tag(context, BLE_NAME_TAG_BASE | (uint32_t)index,
                            (uint8_t *)&saved, sizeof(saved));
    if (size != sizeof(saved) || saved.version != identity.version ||
        saved.address_type != identity.address_type ||
        memcmp(saved.address, identity.address, sizeof(saved.address)) != 0 ||
        !saved.name[0] || !memchr(saved.name, 0, sizeof(saved.name))) return;
    uni_hid_device_set_name(device, saved.name);
}

void ultrausbt_save_ble_name(uni_hid_device_t *device) {
    // Never replace a useful saved name with the generic classification fallback.
    if (!device->name[0] || strcmp(device->name, "BLE mouse") == 0 ||
        strcmp(device->name, "BLE keyboard") == 0 || strcmp(device->name, "BLE joystick") == 0 ||
        strcmp(device->name, "BLE gamepad") == 0) return;
    struct ble_name_record record = {0}, saved = {0};
    int index = ble_name_identity(device, &record);
    if (index < 0) return;
    snprintf(record.name, sizeof(record.name), "%.*s", (int)sizeof(record.name) - 1, device->name);
    const btstack_tlv_t *tlv;
    void *context;
    btstack_tlv_get_instance(&tlv, &context);
    if (!tlv) return;
    uint32_t tag = BLE_NAME_TAG_BASE | (uint32_t)index;
    int size = tlv->get_tag(context, tag, (uint8_t *)&saved, sizeof(saved));
    if (size == sizeof(saved) && memcmp(&saved, &record, sizeof(record)) == 0) return;
    if (tlv->store_tag(context, tag, (const uint8_t *)&record, sizeof(record)))
        loge("[BLE-RECONNECT] Could not persist display name\n");
}

void ultrausbt_clear_ble_names(void) {
    const btstack_tlv_t *tlv;
    void *context;
    btstack_tlv_get_instance(&tlv, &context);
    if (!tlv) return;
    for (int i = 0; i < le_device_db_max_count() && i <= 255; i++)
        tlv->delete_tag(context, BLE_NAME_TAG_BASE | (uint32_t)i);
}
