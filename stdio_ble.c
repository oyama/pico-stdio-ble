/*
 * Copyright 2025, Hiroyuki OYAMA
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <inttypes.h>
#include <stdio.h>

#include "pico/btstack_cyw43.h"
#include "pico/cyw43_arch.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "pico/stdlib.h"
#include "pico_stdio_ble.h"

#include "ble/gatt-service/nordic_spp_service_server.h"
#include "btstack.h"

// clang-format off
static uint8_t adv_data[] = {
    // Flags general discoverable, BR/EDR not supported
    2, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    // Name
    5, BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME, 'P', 'i', 'c', 'o',
    // UUID ...
    17, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS, 0x9e, 0xca, 0xdc, 0x24, 0xe, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x1, 0x0, 0x40, 0x6e,
};
// clang-format on
const uint8_t adv_data_len = sizeof(adv_data);

static btstack_packet_callback_registration_t hci_event_callback_registration;

typedef struct {
    int le_notification_enabled;
    hci_con_handle_t connection_handle;
    const char *buffer;
    int size;
    btstack_context_callback_registration_t send_request;
} nordic_spp_le_streamer_connection_t;

static nordic_spp_le_streamer_connection_t streamer_connection;

static uint16_t att_read_callback(hci_con_handle_t connection_handle, uint16_t att_handle,
                                  uint16_t offset, uint8_t *buffer, uint16_t buffer_size) {
    (void)connection_handle;
    if (att_handle == ATT_CHARACTERISTIC_GAP_DEVICE_NAME_01_VALUE_HANDLE) {
        bd_addr_t local_addr;
        gap_local_bd_addr(local_addr);
        const char *mac = bd_addr_to_str(local_addr);
        char device_name[] = "Pico 00:00:00:00:00:00";
        memcpy(device_name + 5, mac, strlen(mac));
        return att_read_callback_handle_blob((const uint8_t *)device_name, strlen(device_name),
                                             offset, buffer, buffer_size);
    }
    return 0;
}

static hci_con_handle_t con_handle;

static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet,
                               uint16_t size) {
    (void)channel;
    (void)size;

    uint16_t conn_interval;
    bd_addr_t local_addr;

    if (packet_type != HCI_EVENT_PACKET)
        return;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING)
                return;
            gap_local_bd_addr(local_addr);
            const char *mac = bd_addr_to_str(local_addr);
            printf(
                "BTstack up and running on %s, please run Nordic UART Servier client -> stdout to "
                "connect.\n",
                mac);

            // setup advertisements
            uint16_t adv_int_min = 800;
            uint16_t adv_int_max = 800;
            uint8_t adv_type = 0;
            bd_addr_t null_addr;
            memset(null_addr, 0, 6);
            gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07,
                                          0x00);
            gap_advertisements_set_data(adv_data_len, (uint8_t *)adv_data);
            gap_advertisements_enable(1);
            break;
        case HCI_EVENT_META_GAP:
            switch (hci_event_gap_meta_get_subevent_code(packet)) {
                case GAP_SUBEVENT_LE_CONNECTION_COMPLETE:
                    // print connection parameters (without using float operations)
                    con_handle = gap_subevent_le_connection_complete_get_connection_handle(packet);
                    conn_interval = gap_subevent_le_connection_complete_get_conn_interval(packet);
                    printf("LE Connection - Connection Interval: %u.%02u ms\n",
                           conn_interval * 125 / 100, 25 * (conn_interval & 3));
                    printf("LE Connection - Connection Latency: %u\n",
                           gap_subevent_le_connection_complete_get_conn_latency(packet));

                    // request min con interval 15 ms for iOS 11+
                    printf("LE Connection - Request 15 ms connection interval\n");
                    gap_request_connection_parameter_update(con_handle, 12, 12, 4, 0x0048);
                    break;
                default:
                    break;
            }
            break;

        case HCI_EVENT_LE_META:
            switch (hci_event_le_meta_get_subevent_code(packet)) {
                case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
                    // print connection parameters (without using float operations)
                    con_handle =
                        hci_subevent_le_connection_update_complete_get_connection_handle(packet);
                    conn_interval =
                        hci_subevent_le_connection_update_complete_get_conn_interval(packet);
                    printf(
                        "LE Connection - Connection Param update - connection interval %u.%02u ms, "
                        "latency %u\n",
                        conn_interval * 125 / 100, 25 * (conn_interval & 3),
                        hci_subevent_le_connection_update_complete_get_conn_latency(packet));
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

static void nordic_spp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet,
                                      uint16_t size) {
    (void)channel;

    hci_con_handle_t con_handle;
    switch (packet_type) {
        case HCI_EVENT_PACKET:
            if (hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META)
                break;
            switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
                case GATTSERVICE_SUBEVENT_SPP_SERVICE_CONNECTED:
                    con_handle = gattservice_subevent_spp_service_connected_get_con_handle(packet);
                    streamer_connection.connection_handle = con_handle;
                    streamer_connection.buffer = "";
                    streamer_connection.size = 0;
                    streamer_connection.le_notification_enabled = 1;
                    printf("GATT Nordic UART Service client connected\n");
                    break;
                case GATTSERVICE_SUBEVENT_SPP_SERVICE_DISCONNECTED:
                    con_handle =
                        gattservice_subevent_spp_service_disconnected_get_con_handle(packet);
                    // free connection
                    printf("GATT Nordic UART Service client disconnected\n");
                    streamer_connection.le_notification_enabled = 0;
                    streamer_connection.connection_handle = HCI_CON_HANDLE_INVALID;
                    break;
                default:
                    break;
            }
            break;
        case RFCOMM_DATA_PACKET:
            // TODO: Received data can be read out with `stdio_ble_in_chars`
            printf("Recive message=\"%s\" size=%u\n", packet, size);
            break;
        default:
            break;
    }
}

static void att_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet,
                               uint16_t size) {
    (void)channel;
    (void)size;

    if (packet_type != HCI_EVENT_PACKET)
        return;

    switch (hci_event_packet_get_type(packet)) {
        case ATT_EVENT_CONNECTED:
            // setup new
            streamer_connection.buffer = "";
            streamer_connection.size = 0;
            streamer_connection.connection_handle = att_event_connected_get_handle(packet);
            break;
        case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        default:
            break;
    }
}

static void send_stdout_callback(void *some_context) {
    (void)some_context;

    if (streamer_connection.connection_handle == HCI_CON_HANDLE_INVALID)
        return;
    if (!streamer_connection.le_notification_enabled)
        return;

    nordic_spp_service_server_send(streamer_connection.connection_handle,
                                   (uint8_t *)streamer_connection.buffer, streamer_connection.size);
}

static void stdio_ble_out_chars(const char *buffer, int length) {
    if (streamer_connection.connection_handle == HCI_CON_HANDLE_INVALID)
        return;

    streamer_connection.le_notification_enabled = 1;

    streamer_connection.buffer = buffer;
    streamer_connection.size = length;
    streamer_connection.send_request.callback = &send_stdout_callback;
    nordic_spp_service_server_request_can_send_now(&streamer_connection.send_request,
                                                   streamer_connection.connection_handle);
}

static int stdio_ble_in_chars(char *buffer, int length) {
    (void)buffer;
    (void)length;

    // FIXME
    return 0;
}


stdio_driver_t stdio_ble = {.out_chars = stdio_ble_out_chars,
                            .in_chars = stdio_ble_in_chars,
                            .crlf_enabled = 0};

int stdio_ble_init(void) {
    cyw43_arch_init();

    hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    l2cap_init();
    sm_init();

    att_server_init(profile_data, att_read_callback, NULL);

    // setup Nordic SPP service
    nordic_spp_service_server_init(&nordic_spp_packet_handler);
    att_server_register_packet_handler(att_packet_handler);

    hci_power_control(HCI_POWER_ON);

    stdio_set_driver_enabled(&stdio_ble, true);

    return 0;
}
