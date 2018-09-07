/**
 * Copyright (c) 2017, Arm Limited and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdio.h>

#include "lorawan/LoRaWANInterface.h"
#include "lorawan/system/lorawan_data_structures.h"
#include "events/EventQueue.h"
#include "tiny-aes.h"

// Application helpers
#include "DummySensor.h"
#include "trace_helper.h"
#include "lora_radio_helper.h"

using namespace events;

// Max payload size can be LORAMAC_PHY_MAXPAYLOAD.
// This example only communicates with much shorter messages (<30 bytes).
// If longer messages are used, these buffers must be changed accordingly.
uint8_t tx_buffer[30];
uint8_t rx_buffer[30];

/*
 * Sets up an application dependent transmission timer in ms. Used only when Duty Cycling is off for testing
 */
#define TX_TIMER                        10000

/**
 * Maximum number of events for the event queue.
 * 10 is the safe number for the stack events, however, if application
 * also uses the queue for whatever purposes, this number should be increased.
 */
#define MAX_NUMBER_OF_EVENTS            10

/**
 * Maximum number of retries for CONFIRMED messages before giving up
 */
#define CONFIRMED_MSG_RETRY_COUNTER     3

/**
 * Dummy pin for dummy sensor
 */
#define PC_9                            0

/**
 * Dummy sensor class object
 */
DS1820  ds1820(PC_9);

/**
* This event queue is the global event queue for both the
* application and stack. To conserve memory, the stack is designed to run
* in the same thread as the application and the application is responsible for
* providing an event queue to the stack that will be used for ISR deferment as
* well as application information event queuing.
*/
static EventQueue ev_queue(MAX_NUMBER_OF_EVENTS * EVENTS_EVENT_SIZE);

/**
 * Event handler.
 *
 * This will be passed to the LoRaWAN stack to queue events for the
 * application which in turn drive the application.
 */
static void lora_event_handler(lorawan_event_t event);

/**
 * Constructing Mbed LoRaWANInterface and passing it down the radio object.
 */
static LoRaWANInterface lorawan(radio);

/**
 * Application specific callbacks
 */
static lorawan_app_callbacks_t callbacks;

static bool in_skt_join = true;
static uint8_t deveui[] = { 0x00, 0x80, 0x00, 0x00, 0x04, 0x00, 0x37, 0xD1 };
static uint8_t appeui[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A };
uint8_t pseudo_appkey[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06 };
static uint8_t real_app_key_output[16];
uint8_t netid[3] = { 0xd, 0x0, 0x0 }; // 13 = SKT

/**
 * Entry point for application
 */
int main (void)
{
    // setup tracing
    setup_trace();

    // stores the status of a call to LoRaWAN protocol
    lorawan_status_t retcode;

    // Initialize LoRaWAN stack
    if (lorawan.initialize(&ev_queue) != LORAWAN_STATUS_OK) {
        printf("\r\n LoRa initialization failed! \r\n");
        return -1;
    }

    printf("\r\n Mbed LoRaWANStack initialized \r\n");

    // prepare application callbacks
    callbacks.events = mbed::callback(lora_event_handler);
    lorawan.add_app_callbacks(&callbacks);

    // Set number of retries in case of CONFIRMED messages
    if (lorawan.set_confirmed_msg_retries(CONFIRMED_MSG_RETRY_COUNTER)
                                          != LORAWAN_STATUS_OK) {
        printf("\r\n set_confirmed_msg_retries failed! \r\n\r\n");
        return -1;
    }

    printf("\r\n CONFIRMED message retries : %d \r\n",
           CONFIRMED_MSG_RETRY_COUNTER);

    // Enable adaptive data rate
    if (lorawan.enable_adaptive_datarate() != LORAWAN_STATUS_OK) {
        printf("\r\n enable_adaptive_datarate failed! \r\n");
        return -1;
    }

    printf("\r\n Adaptive data  rate (ADR) - Enabled \r\n");

    lorawan_connect_t connect_params;
    connect_params.connect_type = LORAWAN_CONNECTION_OTAA;
    connect_params.connection_u.otaa.dev_eui = deveui;
    connect_params.connection_u.otaa.app_eui = appeui;
    connect_params.connection_u.otaa.app_key = pseudo_appkey;

    retcode = lorawan.connect(connect_params);

    if (retcode == LORAWAN_STATUS_OK ||
        retcode == LORAWAN_STATUS_CONNECT_IN_PROGRESS) {
    } else {
        printf("\r\n Connection error, code = %d \r\n", retcode);
        return -1;
    }

    printf("\r\n Connection - In Progress ...\r\n");

    // make your event queue dispatching events forever
    ev_queue.dispatch_forever();

    return 0;
}

/**
 * Sends a message to the Network Server
 */
static void send_message()
{
    uint16_t packet_len;
    int16_t retcode;
    float sensor_value;

    if (ds1820.begin()) {
        ds1820.startConversion();
        sensor_value = ds1820.read();
        printf("\r\n Dummy Sensor Value = %3.1f \r\n", sensor_value);
        ds1820.startConversion();
    } else {
        printf("\r\n No sensor found \r\n");
        return;
    }

    packet_len = sprintf((char*) tx_buffer, "Dummy Sensor Value is %3.1f",
                    sensor_value);

    retcode = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_buffer, packet_len,
                           MSG_UNCONFIRMED_FLAG);

    if (retcode < 0) {
        retcode == LORAWAN_STATUS_WOULD_BLOCK ? printf("send - WOULD BLOCK\r\n")
                : printf("\r\n send() - Error code %d \r\n", retcode);

        if (retcode == LORAWAN_STATUS_WOULD_BLOCK) {
            //retry in 3 seconds
            if (MBED_CONF_LORA_DUTY_CYCLE_ON) {
                ev_queue.call_in(3000, send_message);
            }
        }
        return;
    }

    printf("\r\n %d bytes scheduled for transmission \r\n", retcode);
    memset(tx_buffer, 0, sizeof(tx_buffer));
}

/**
 * Receive a message from the Network Server
 */
static void receive_message()
{
    int16_t retcode;
    retcode = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_buffer,
                              sizeof(rx_buffer),
                              MSG_CONFIRMED_FLAG|MSG_UNCONFIRMED_FLAG);

    if (retcode < 0) {
        printf("\r\n receive() - Error code %d \r\n", retcode);
        return;
    }

    printf(" Data:");

    for (uint8_t i = 0; i < retcode; i++) {
        printf("%x", rx_buffer[i]);
    }

    printf("\r\n Data Length: %d\r\n", retcode);

    memset(rx_buffer, 0, sizeof(rx_buffer));
}

typedef enum {
    APP_KEY_ALLOC_REQ = 0x0,
    APP_KEY_ALLOC_ANS = 0x1,
    APP_KEY_REPORT_REQ = 0x2,
    APP_KEY_REPORT_ANS = 0x3
} skt_join_messages_t;

static const uint8_t SKT_JOIN_VERSION = 0;

static uint8_t skt_nonce[3] = { 0x0 };

/**
 * Event handler
 */
static void lora_event_handler(lorawan_event_t event)
{
    switch (event) {
        case CONNECTED:
            printf("\r\n Connection - Successful \r\n");

            if (in_skt_join) {
                uint8_t app_key_alloc_req[] = { SKT_JOIN_VERSION, APP_KEY_ALLOC_REQ, 0x0 };
                uint16_t retcode = lorawan.send(223, app_key_alloc_req, sizeof(app_key_alloc_req), MSG_CONFIRMED_FLAG);
            }
            else {
                ev_queue.call_every(10000, &send_message);
            }

            break;
        case DISCONNECTED:
            if (in_skt_join) {
                in_skt_join = false;

                // now time to rejoin
                const uint8_t aes_input[16] = { skt_nonce[0], skt_nonce[1], skt_nonce[2], netid[0], netid[1], netid[2] /*3 bytes netid */, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
                AES_ECB_encrypt(aes_input, pseudo_appkey, real_app_key_output, 16);

                printf("aes input: ");
                for (size_t ix = 0; ix < 16; ix++) {
                    printf("%02x ", aes_input[ix]);
                }
                printf("\n");

                printf("real_app_key_output: ");
                for (size_t ix = 0; ix < 16; ix++) {
                    printf("%02x ", real_app_key_output[ix]);
                }
                printf("\n");

                lorawan_connect_t connect_params;
                connect_params.connect_type = LORAWAN_CONNECTION_OTAA;
                connect_params.connection_u.otaa.dev_eui = deveui;
                connect_params.connection_u.otaa.app_eui = appeui;
                connect_params.connection_u.otaa.app_key = real_app_key_output;

                uint16_t retcode = lorawan.connect(connect_params);
                printf("connnect returned %u\n", retcode);
                return;
            }

            ev_queue.break_dispatch();
            printf("\r\n Disconnected Successfully \r\n");
            break;
        case TX_DONE:
            printf("\r\n Message Sent to Network Server \r\n");
            break;
        case TX_TIMEOUT:
        case TX_ERROR:
        case TX_CRYPTO_ERROR:
        case TX_SCHEDULING_ERROR:
            printf("\r\n Transmission Error - EventCode = %d \r\n", event);
            break;
        case RX_DONE:
            printf("\r\n Received message from Network Server \r\n");

            if (in_skt_join) {
                uint8_t rx_data[255];
                uint16_t retcode = lorawan.receive(223, rx_data, 255, MSG_CONFIRMED_FLAG|MSG_UNCONFIRMED_FLAG);

                printf("Got skt join message (%u): ", retcode);
                for (size_t ix = 0; ix < retcode; ix++) {
                    printf("%02x ", rx_data[ix]);
                }
                printf("\n");

                // retcode below zero means error, and we need at least 3 bytes
                if (retcode < 3) return;

                // byte 0x0 = version
                // byte 0x1 = message type
                // byte 0x2 = payload length
                // 3 bytes payload = nonce

                if (rx_data[1] == APP_KEY_ALLOC_ANS && rx_data[2] == 0x3 && retcode == 6) {
                    printf("App key alloc ans\n");

                    printf("Nonce: %02x %02x %02x\n", rx_data[3], rx_data[4], rx_data[5]);

                    memcpy(skt_nonce, rx_data+3, 3);

                    uint8_t real_app_key_alloc_req[] = { SKT_JOIN_VERSION, APP_KEY_REPORT_REQ, 0x0 };
                    retcode = lorawan.send(223, real_app_key_alloc_req, sizeof(real_app_key_alloc_req), MSG_CONFIRMED_FLAG);
                }
                else if (rx_data[1] == APP_KEY_REPORT_ANS) {
                    printf("App key report ans\n");

                    lorawan.disconnect();
                }
            }
            else {
                receive_message();
            }
            break;
        case RX_TIMEOUT:
        case RX_ERROR:
            printf("\r\n Error in reception - Code = %d \r\n", event);
            break;
        case JOIN_FAILURE:
            printf("\r\n OTAA Failed - Check Keys \r\n");
            break;
        case UPLINK_REQUIRED:
            printf("\r\n Uplink required by NS \r\n");
            if (MBED_CONF_LORA_DUTY_CYCLE_ON) {
                send_message();
            }
            break;
        default:
            MBED_ASSERT("Unknown Event");
    }
}

// EOF
