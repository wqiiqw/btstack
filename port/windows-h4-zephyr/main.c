/*
 * Copyright (C) 2014 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL BLUEKITCHEN
 * GMBH OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */

#define BTSTACK_FILE__ "main.c"

// *****************************************************************************
//
// minimal setup for HCI code
//
// *****************************************************************************

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btstack_config.h"

#include "ble/le_device_db_tlv.h"
#include "bluetooth_company_id.h"
#include "btstack_audio.h"
#include "btstack_chipset_zephyr.h"
#include "btstack_debug.h"
#include "btstack_event.h"
#include "btstack_memory.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_windows.h"
#include "btstack_stdin.h"
#include "btstack_stdin_windows.h"
#include "btstack_tlv_windows.h"
#include "hal_led.h"
#include "hci.h"
#include "hci_dump.h"
#include "hci_dump_windows_fs.h"
#include "hci_transport.h"
#include "hci_transport_h4.h"

int btstack_main(int argc, const char * argv[]);

static hci_transport_config_uart_t config = {
        HCI_TRANSPORT_CONFIG_UART,
        500000,  //115200  500000
        0,  // main baudrate
        1,  // flow control
        NULL,
};

static btstack_packet_callback_registration_t hci_event_callback_registration;

static bd_addr_t static_address;

#define TLV_DB_PATH_PREFIX "btstack_"
#define TLV_DB_PATH_POSTFIX ".tlv"
static char tlv_db_path[100];
static const btstack_tlv_t * tlv_impl;
static btstack_tlv_windows_t   tlv_context;
static bool shutdown_triggered;

static void packet_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    const uint8_t* params;
    if (packet_type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)){
        case BTSTACK_EVENT_STATE:
            switch (btstack_event_state_get_state(packet)){
                case HCI_STATE_WORKING:
                    printf("BTstack up and running as %s\n",  bd_addr_to_str(static_address));
                    btstack_strcpy(tlv_db_path, sizeof(tlv_db_path), TLV_DB_PATH_PREFIX);
                    btstack_strcat(tlv_db_path, sizeof(tlv_db_path), bd_addr_to_str_with_delimiter(static_address, '-'));
                    btstack_strcat(tlv_db_path, sizeof(tlv_db_path), TLV_DB_PATH_POSTFIX);
                    tlv_impl = btstack_tlv_windows_init_instance(&tlv_context, tlv_db_path);
                    btstack_tlv_set_instance(tlv_impl, &tlv_context);
#ifdef ENABLE_BLE
                    le_device_db_tlv_configure(tlv_impl, &tlv_context);
#endif
                    break;
                case HCI_STATE_OFF:
                    btstack_tlv_windows_deinit(&tlv_context);
                    if (!shutdown_triggered) break;
                    // reset stdin
                    btstack_stdin_reset();
                    log_info("Good bye, see you.\n");
                    exit(0);
                    break;
                default:
                    break;
            }
            break;
        case HCI_EVENT_COMMAND_COMPLETE:
            switch (hci_event_command_complete_get_command_opcode(packet)){
                case HCI_OPCODE_HCI_ZEPHYR_READ_STATIC_ADDRESS:
                    params = hci_event_command_complete_get_return_parameters(packet);
                    if(params[0] != 0)
                        break;
                    if(size < 13)
                        break;
                    reverse_48(&params[2], static_address);
                    gap_random_address_set(static_address);
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

static void trigger_shutdown(void){
    printf("CTRL-C - SIGINT received, shutting down..\n");
    log_info("sigint_handler: shutting down");
    shutdown_triggered = true;
    hci_power_control(HCI_POWER_OFF);
}

static int led_state = 0;
void hal_led_toggle(void){
    led_state = 1 - led_state;
    printf("LED State %u\n", led_state);
}

static void print_usage(const char* prog) {
    printf("Usage: %s [-u COM_PORT] [-b BAUDRATE] [-h DUMP_FILE]\n", prog);
    printf("  -u COM_PORT    : UART device name (e.g., \\\\.\\COM44)\n");
    printf("  -b BAUDRATE    : UART baudrate (positive integer)\n");
    printf("  -h DUMP_FILE   : Path to HCI dump .pklg file\n");
    printf("\nIf no arguments are provided, default values are used:\n");
    printf("  COM_PORT       : \\\\.\\COM44\n");
    printf("  BAUDRATE       : 500000\n");
    printf("  DUMP_FILE      : hci_dump.pklg\n");
}

int main(int argc, const char* argv[]) {

    const char* default_com_port = "\\\\.\\COM44";
    uint32_t default_baudrate = 500000;
    const char* default_dump_path = "hci_dump.pklg";

    const char* pklg_path;
    int baud;
    int i;

    // Set defaults
    config.device_name = default_com_port;
    config.baudrate_main = default_baudrate;
    pklg_path = default_dump_path;

    // Argument parsing
    i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-u") == 0) {
            if (i + 1 < argc) {
                config.device_name = argv[i + 1];
                i += 2;
            }
            else {
                fprintf(stderr, "Error: -u requires a COM port (e.g., -u \\\\.\\COM44)\n");
                print_usage(argv[0]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-b") == 0) {
            if (i + 1 < argc) {
                baud = atoi(argv[i + 1]);
                if (baud <= 0) {
                    fprintf(stderr, "Error: Invalid baudrate '%s'. Must be a positive integer.\n", argv[i + 1]);
                    print_usage(argv[0]);
                    return 1;
                }
                config.baudrate_main = (uint32_t)baud;
                i += 2;
            }
            else {
                fprintf(stderr, "Error: -b requires a baudrate value (e.g., -b 500000)\n");
                print_usage(argv[0]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-h") == 0) {
            if (i + 1 < argc) {
                pklg_path = argv[i + 1];
                i += 2;
            }
            else {
                fprintf(stderr, "Error: -h requires a file path (e.g., -h dump.pklg)\n");
                print_usage(argv[0]);
                return 1;
            }
        }
        else {
            fprintf(stderr, "Error: Unknown argument '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // === BTstack Initialization ===
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_windows_get_instance());

    // HCI dump log
    hci_dump_windows_fs_open(pklg_path, HCI_DUMP_PACKETLOGGER);
    {
        const hci_dump_t* hci_dump_impl = hci_dump_windows_fs_get_instance();
        hci_dump_init(hci_dump_impl);
    }

    printf("Packet Log: %s\n", pklg_path);
    printf("H4 device : %s\n", config.device_name);
    printf("Baudrate  : %u\n", config.baudrate_main);

    {
        const btstack_uart_block_t* uart_driver = btstack_uart_block_windows_instance();
        const hci_transport_t* transport = hci_transport_h4_instance(uart_driver);
        hci_init(transport, (void*)&config);
    }
    hci_set_chipset(btstack_chipset_zephyr_instance());

#ifdef HAVE_PORTAUDIO
    btstack_audio_sink_set_instance(btstack_audio_portaudio_sink_get_instance());
    btstack_audio_source_set_instance(btstack_audio_portaudio_source_get_instance());
#endif

    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    btstack_stdin_windows_init();
    btstack_stdin_window_register_ctrl_c_callback(&trigger_shutdown);

    btstack_main(argc, argv);
    sm_init();
    btstack_run_loop_execute();

    return 0;
}

