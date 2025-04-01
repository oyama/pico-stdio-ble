/*
 * Copyright 2025, Hiroyuki OYAMA.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <inttypes.h>
#include <stdio.h>
#include "pico/stdio/driver.h"
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "btstack.h"
#include "pico/btstack_cyw43.h"
#include "pico/cyw43_arch.h"

#include "ble/gatt-service/nordic_spp_service_server.h"
#include "pico_stdio_ble.h"


#define REPORT_INTERVAL_MS 3000
#define MAX_NR_CONNECTIONS 3


static uint8_t adv_data[] = {
    // Flags general discoverable, BR/EDR not supported
    2, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    // Name
    0x05, BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME, 'P', 'i', 'c', 'o',
    // UUID ...
    17, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS, 0x9e, 0xca, 0xdc, 0x24, 0xe, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x1, 0x0, 0x40, 0x6e,
};
const uint8_t adv_data_len = sizeof(adv_data);

#define ADVERTISEMENTS_FIELD_HEADER  2
static uint8_t complete_local_name[] = {
    0x17, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 'P', 'i', 'c', 'o', ' ', '0', '0', ':', '0', '0', ':', '0', '0', ':', '0', '0', ':', '0', '0', ':', '0', '0'
};
const uint8_t scan_response_data_len = sizeof(complete_local_name);


static btstack_packet_callback_registration_t hci_event_callback_registration;

typedef struct {
    char name;
    int le_notification_enabled;
    hci_con_handle_t connection_handle;
    char buffer[1024];
    int  size;
    uint32_t data_sent;
    uint32_t data_start;
    btstack_context_callback_registration_t send_request;
} nordic_spp_le_streamer_connection_t;

static nordic_spp_le_streamer_connection_t nordic_spp_le_streamer_connections[MAX_NR_CONNECTIONS];

static int connection_index;

static void init_connections(void){
    // track connections
    int i;
    for (i=0;i<MAX_NR_CONNECTIONS;i++){
        nordic_spp_le_streamer_connections[i].connection_handle = HCI_CON_HANDLE_INVALID;
        nordic_spp_le_streamer_connections[i].name = 'A' + i;
    }
}

static nordic_spp_le_streamer_connection_t * connection_for_conn_handle(hci_con_handle_t conn_handle){
    int i;
    for (i=0;i<MAX_NR_CONNECTIONS;i++){
        if (nordic_spp_le_streamer_connections[i].connection_handle == conn_handle) return &nordic_spp_le_streamer_connections[i];
    }
    return NULL;
}

static void next_connection_index(void){
    connection_index++;
    if (connection_index == MAX_NR_CONNECTIONS){
        connection_index = 0;
    }
}

static void test_reset(nordic_spp_le_streamer_connection_t * context){
    context->data_start = btstack_run_loop_get_time_ms();
    context->data_sent = 0;
}

static hci_con_handle_t con_handle;

static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    uint16_t conn_interval;
    bd_addr_t local_addr;

    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
            gap_local_bd_addr(local_addr);
            const char *mac = bd_addr_to_str(local_addr);
            printf("BTstack up and running on %s, please run Nordic UART Servier client -> UART to connect.\n", mac);

            // setup advertisements
            uint16_t adv_int_min = 800;
            uint16_t adv_int_max = 800;
            uint8_t adv_type = 0;
            bd_addr_t null_addr;
            memset(null_addr, 0, 6);
            gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07, 0x00);
            gap_advertisements_set_data(adv_data_len, (uint8_t *)adv_data);
            gap_scan_response_set_data(scan_response_data_len, complete_local_name);
            gap_advertisements_enable(1);
            break;
        case HCI_EVENT_META_GAP:
            switch (hci_event_gap_meta_get_subevent_code(packet)) {
                case GAP_SUBEVENT_LE_CONNECTION_COMPLETE:
                    // print connection parameters (without using float operations)
                    con_handle = gap_subevent_le_connection_complete_get_connection_handle(packet);
                    conn_interval = gap_subevent_le_connection_complete_get_conn_interval(packet);
                    printf("LE Connection - Connection Interval: %u.%02u ms\n", conn_interval * 125 / 100, 25 * (conn_interval & 3));
                    printf("LE Connection - Connection Latency: %u\n", gap_subevent_le_connection_complete_get_conn_latency(packet));

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
                    con_handle = hci_subevent_le_connection_update_complete_get_connection_handle(packet);
                    conn_interval = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
                    printf("LE Connection - Connection Param update - connection interval %u.%02u ms, latency %u\n", conn_interval * 125 / 100,
                        25 * (conn_interval & 3), hci_subevent_le_connection_update_complete_get_conn_latency(packet));
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

static void nordic_spp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    hci_con_handle_t con_handle;
    nordic_spp_le_streamer_connection_t * context;
    switch (packet_type){
        case HCI_EVENT_PACKET:
            if (hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META) break;
            switch (hci_event_gattservice_meta_get_subevent_code(packet)){
                case GATTSERVICE_SUBEVENT_SPP_SERVICE_CONNECTED:
                    con_handle = gattservice_subevent_spp_service_connected_get_con_handle(packet);
                    context = connection_for_conn_handle(con_handle);
                    if (!context) break;
                    printf("%c: Nordic SPP connected\n", context->name);
                    context->le_notification_enabled = 1;
                    break;
                case GATTSERVICE_SUBEVENT_SPP_SERVICE_DISCONNECTED:
                    con_handle = gattservice_subevent_spp_service_disconnected_get_con_handle(packet);
                    context = connection_for_conn_handle(con_handle);
                    if (!context) break;
                    // free connection
                    printf("%c: Nordic SPP disconnected\n", context->name);
                    context->le_notification_enabled = 0;
                    context->connection_handle = HCI_CON_HANDLE_INVALID;
                    break;
                default:
                    break;
            }
            break;
        case RFCOMM_DATA_PACKET:
            //TODO: Received data can be read out with `stdio_ble_in_chars`
            printf("Recive message=\"%s\" size=%u\n", packet, size);
            break;
        default:
            break;
    }
}

static void att_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    int mtu;
    nordic_spp_le_streamer_connection_t *context;

    switch (hci_event_packet_get_type(packet)) {
        case ATT_EVENT_CONNECTED:
            // setup new_
            context = connection_for_conn_handle(HCI_CON_HANDLE_INVALID);
            if (!context) break;
            context->size = 0;
            context->buffer[0] = '\0';
            context->connection_handle = att_event_connected_get_handle(packet);
            break;
        case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        default:
            break;
    }
}

static void send_stdout_callback(void * some_context){
    UNUSED(some_context);

    // find next active streaming connection
    int old_connection_index = connection_index;
    while (1){
        // active found?
        if ((nordic_spp_le_streamer_connections[connection_index].connection_handle != HCI_CON_HANDLE_INVALID) &&
            (nordic_spp_le_streamer_connections[connection_index].le_notification_enabled)) break;

        // check next
        next_connection_index();

        // none found
        if (connection_index == old_connection_index) return;
    }

    nordic_spp_le_streamer_connection_t *context = &nordic_spp_le_streamer_connections[connection_index];
    nordic_spp_service_server_send(context->connection_handle, (uint8_t *)context->buffer, context->size);

    // check next
    next_connection_index();
}

static void stdio_ble_out_chars(const char *buffer, int length) {
    if (con_handle == HCI_CON_HANDLE_INVALID)
        return;

    nordic_spp_le_streamer_connection_t *context = connection_for_conn_handle(con_handle);
    if (!context)
        return;

    context->le_notification_enabled = 1;
    test_reset(context);

    memcpy(context->buffer, buffer, length);
    context->size = length;
    context->send_request.callback = &send_stdout_callback;
    nordic_spp_service_server_request_can_send_now(&context->send_request, context->connection_handle);
}

static int stdio_ble_in_chars(char *buf, int length) {
    return 0;
}

static void stdio_ble_out_flush(void) {
}

stdio_driver_t stdio_ble = {
    .out_chars = stdio_ble_out_chars,
    .out_flush = stdio_ble_out_flush,
    .in_chars = stdio_ble_in_chars,
    .crlf_enabled = 0
};

int stdio_ble_init(void) {
    cyw43_arch_init();

    hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    l2cap_init();
    sm_init();

    att_server_init(profile_data, NULL, NULL);

    // setup Nordic SPP service
    nordic_spp_service_server_init(&nordic_spp_packet_handler);
    att_server_register_packet_handler(att_packet_handler);

    // init client state
    init_connections();

    hci_power_control(HCI_POWER_ON);

    stdio_set_driver_enabled(&stdio_ble, true);
}
