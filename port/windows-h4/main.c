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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btstack_config.h"

#include "ble/le_device_db_tlv.h"
#include "bluetooth_company_id.h"
#include "btstack_audio.h"
#include "btstack_chipset_bcm.h"
#include "btstack_chipset_cc256x.h"
#include "btstack_chipset_csr.h"
#include "btstack_chipset_em9301.h"
#include "btstack_chipset_stlc2500d.h"
#include "btstack_chipset_tc3566x.h"
#include "btstack_debug.h"
#include "btstack_event.h"
#include "btstack_memory.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_windows.h"
#include "btstack_stdin.h"
#include "btstack_stdin_windows.h"
#include "btstack_tlv_windows.h"
#include "classic/btstack_link_key_db_tlv.h"
#include "hal_led.h"
#include "hci.h"
#include "hci_dump.h"
#include "hci_dump_windows_fs.h"
#include "hci_transport.h"
#include "hci_transport_h4.h"

int btstack_main(int argc, const char * argv[]);
static void local_version_information_handler(uint8_t * packet);
static void show_usage(const char* program_name);

static hci_transport_config_uart_t config = {
        HCI_TRANSPORT_CONFIG_UART,
        500000,  // default baudrate
        0,  // main baudrate
        0,  // flow control (default off)
        NULL,
};

int is_bcm;

static int led_state = 0;

#define TLV_DB_PATH_PREFIX "btstack_"
#define TLV_DB_PATH_POSTFIX ".tlv"
static char tlv_db_path[100];
static const btstack_tlv_t * tlv_impl;
static btstack_tlv_windows_t   tlv_context;
static bd_addr_t             local_addr;
static bool shutdown_triggered;

void hal_led_toggle(void){
    led_state = 1 - led_state;
    printf("LED State %u\n", led_state);
}

static void trigger_shutdown(void){
    printf("CTRL-C - SIGINT received, shutting down..\n");
    log_info("sigint_handler: shutting down");
    shutdown_triggered = true;
    hci_power_control(HCI_POWER_OFF);
}

static btstack_packet_callback_registration_t hci_event_callback_registration;

static void packet_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    if (packet_type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)){
        case BTSTACK_EVENT_STATE:
            switch (btstack_event_state_get_state(packet)){
                case HCI_STATE_WORKING:
                    gap_local_bd_addr(local_addr);
                    printf("BTstack up and running on %s.\n", bd_addr_to_str(local_addr));
                    btstack_strcpy(tlv_db_path, sizeof(tlv_db_path), TLV_DB_PATH_PREFIX);
                    btstack_strcat(tlv_db_path, sizeof(tlv_db_path), bd_addr_to_str_with_delimiter(local_addr, '-'));
                    btstack_strcat(tlv_db_path, sizeof(tlv_db_path), TLV_DB_PATH_POSTFIX);
                    tlv_impl = btstack_tlv_windows_init_instance(&tlv_context, tlv_db_path);
                    btstack_tlv_set_instance(tlv_impl, &tlv_context);
#ifdef ENABLE_CLASSIC
                    hci_set_link_key_db(btstack_link_key_db_tlv_get_instance(tlv_impl, &tlv_context));
#endif
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
            if (hci_event_command_complete_get_command_opcode(packet) == HCI_OPCODE_HCI_READ_LOCAL_NAME){
                if (hci_event_command_complete_get_return_parameters(packet)[0]) break;
                // terminate, name 248 chars
                packet[6+248] = 0;
                printf("Local name: %s\n", &packet[6]);
                if (is_bcm){
                    btstack_chipset_bcm_set_device_name((const char *)&packet[6]);
                }
            }
            if (hci_event_command_complete_get_command_opcode(packet) == HCI_OPCODE_HCI_READ_LOCAL_VERSION_INFORMATION){
                local_version_information_handler(packet);
            }
            break;
        default:
            break;
    }
}
static void use_fast_uart(void){
    printf("Using 921600 baud.\n");
    config.baudrate_main = 921600;
}

static void local_version_information_handler(uint8_t * packet){
    printf("Local version information:\n");
    uint16_t hci_version    = packet[6];
    uint16_t hci_revision   = little_endian_read_16(packet, 7);
    uint16_t lmp_version    = packet[9];
    uint16_t manufacturer   = little_endian_read_16(packet, 10);
    uint16_t lmp_subversion = little_endian_read_16(packet, 12);
    printf("- HCI Version  0x%04x\n", hci_version);
    printf("- HCI Revision 0x%04x\n", hci_revision);
    printf("- LMP Version  0x%04x\n", lmp_version);
    printf("- LMP Revision 0x%04x\n", lmp_subversion);
    printf("- Manufacturer 0x%04x\n", manufacturer);
    switch (manufacturer){
        case BLUETOOTH_COMPANY_ID_CAMBRIDGE_SILICON_RADIO:
            printf("Cambridge Silicon Radio - CSR chipset, Build ID: %u.\n", hci_revision);
            use_fast_uart();
            hci_set_chipset(btstack_chipset_csr_instance());
            break;
        case BLUETOOTH_COMPANY_ID_TEXAS_INSTRUMENTS_INC:
            printf("Texas Instruments - CC256x compatible chipset.\n");
            if (lmp_subversion != btstack_chipset_cc256x_lmp_subversion()){
                printf("Error: LMP Subversion does not match initscript!");
                printf("Your initscripts is for %s chipset\n", btstack_chipset_cc256x_lmp_subversion() < lmp_subversion ? "an older" : "a newer");
                printf("Please update Makefile to include the appropriate bluetooth_init_cc256???.c file\n");
                exit(10);
            }
            use_fast_uart();
            hci_set_chipset(btstack_chipset_cc256x_instance());
#ifdef ENABLE_EHCILL
            printf("eHCILL enabled.\n");
#else
            printf("eHCILL disable.\n");
#endif
            break;
        case BLUETOOTH_COMPANY_ID_BROADCOM_CORPORATION:
            printf("Broadcom - using BCM driver.\n");
            hci_set_chipset(btstack_chipset_bcm_instance());
            use_fast_uart();
            is_bcm = 1;
            break;
        case BLUETOOTH_COMPANY_ID_ST_MICROELECTRONICS:
            printf("ST Microelectronics - using STLC2500d driver.\n");
            use_fast_uart();
            hci_set_chipset(btstack_chipset_stlc2500d_instance());
            break;
        case BLUETOOTH_COMPANY_ID_EM_MICROELECTRONIC_MARIN_SA:
            printf("EM Microelectronics - using EM9301 driver.\n");
            hci_set_chipset(btstack_chipset_em9301_instance());
            break;
        case BLUETOOTH_COMPANY_ID_NORDIC_SEMICONDUCTOR_ASA:
            printf("Nordic Semiconductor nRF5 chipset.\n");
            break;
        case BLUETOOTH_COMPANY_ID_TOSHIBA_CORP:
            printf("Toshiba - using TC3566x driver.\n");
            hci_set_chipset(btstack_chipset_tc3566x_instance());
            use_fast_uart();
            break;
        default:
            printf("Unknown manufacturer / manufacturer not supported yet.\n");
            break;
    }
}

static char com_port_buffer[20];

static const char* format_com_port(const char* input) {
    // Handle different COM port formats
    if (strncmp(input, "COM", 3) == 0) {
        int port_num = atoi(input + 3);
        if (port_num <= 0) {
            return input; // Invalid, return as-is
        }
        if (port_num < 10) {
            // For COM1-COM9, use simple format
            snprintf(com_port_buffer, sizeof(com_port_buffer), "COM%d", port_num);
        } else {
            // For COM10+, use extended format
            snprintf(com_port_buffer, sizeof(com_port_buffer), "\\\\.\\COM%d", port_num);
        }
        return com_port_buffer;
    } else if (strstr(input, "\\\\.\\COM") != NULL) {
        // Already in extended format, return as-is
        return input;
    } else {
        // Not a COM port format, return as-is
        return input;
    }
}

static void show_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -u <device>     COM port (COM6, COM15, or \\\\.\\COM15 format)\n");
    printf("  -b <baudrate>   UART baudrate (default: 500000)\n");
    printf("  -f <0|1>        Flow control: 0=disabled, 1=enabled (default: 0)\n");
    printf("  -d <path>       HCI dump file path (default: hci_dump.pklg)\n");
    printf("  -h              Show this help message\n");
    printf("\nCOM Port Format:\n");
    printf("  COM1-COM9:      Use 'COM6' format\n");
    printf("  COM10+:         Use 'COM15' or '\\\\.\\COM15' format\n");
    printf("\nExamples:\n");
    printf("  %s -u COM6 -b 115200\n", program_name);
    printf("  %s -u COM15 -f 0\n", program_name);
    printf("  %s -u \\\\.\\COM3 -d my_dump.pklg\n", program_name);
}

int main(int argc, const char * argv[]){
    printf("BTstack on windows booting up\n");

    /// GET STARTED with BTstack ///
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_windows_get_instance());

    // pre-select serial device
    config.device_name = "\\\\.\\COM15"; // Default COM port
    
    // default HCI dump path
    const char * pklg_path = "hci_dump.pklg";
    const char * custom_pklg_path = NULL;

    // parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
            config.device_name = format_com_port(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            config.baudrate_init = atoi(argv[++i]);
            if (config.baudrate_init <= 0) {
                printf("Error: Invalid baudrate specified\n");
                show_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            int flow_control = atoi(argv[++i]);
            if (flow_control != 0 && flow_control != 1) {
                printf("Error: Flow control must be 0 or 1\n");
                show_usage(argv[0]);
                return 1;
            }
            config.flowcontrol = flow_control;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            custom_pklg_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0) {
            show_usage(argv[0]);
            return 0;
        } else {
            printf("Error: Unknown option '%s'\n", argv[i]);
            show_usage(argv[0]);
            return 1;
        }
    }
    
    printf("H4 device: %s\n", config.device_name);
    printf("Baudrate: %u\n", config.baudrate_init);
    printf("Flow control: %s\n", config.flowcontrol ? "enabled" : "disabled");

    // use custom dump path if provided
    if (custom_pklg_path) {
        pklg_path = custom_pklg_path;
    }
    
    // log into file using HCI_DUMP_PACKETLOGGER format
    hci_dump_windows_fs_open(pklg_path, HCI_DUMP_PACKETLOGGER);
    const hci_dump_t * hci_dump_impl = hci_dump_windows_fs_get_instance();
    hci_dump_init(hci_dump_impl);
    printf("Packet Log: %s\n", pklg_path);

    // init HCI
    const btstack_uart_block_t * uart_driver = btstack_uart_block_windows_instance();
    const hci_transport_t * transport = hci_transport_h4_instance(uart_driver);
    hci_init(transport, (void*) &config);

#ifdef HAVE_PORTAUDIO
    btstack_audio_sink_set_instance(btstack_audio_portaudio_sink_get_instance());
    btstack_audio_source_set_instance(btstack_audio_portaudio_source_get_instance());
#endif

    // inform about BTstack state
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // setup stdin to handle CTRL-c
    btstack_stdin_windows_init();
    btstack_stdin_window_register_ctrl_c_callback(&trigger_shutdown);

    // setup app (no arguments passed as they're handled here)
    btstack_main(0, NULL);

    // go
    btstack_run_loop_execute();

    return 0;
}
