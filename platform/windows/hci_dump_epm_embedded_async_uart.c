/*
 * Copyright (C) 2014 Blue Kitchen GmbH
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

#define BTSTACK_FILE__ "hci_dump_epm_embedded_async_uart.c"

/*
 *  Dump HCI trace via async UART in H4 format
 */

#include "bluetooth.h"          // ← Added for H4 packet type definitions
#include "hci_dump.h"
#include "btstack_config.h"
#include "hci.h"
#include "hci_cmd.h"
#include "log_async_write.h"
#include <stdio.h>
#include <string.h>

// Buffer for H4 packet formatting
#define H4_PACKET_BUFFER_SIZE 1024
static uint8_t h4_packet_buffer[H4_PACKET_BUFFER_SIZE];

static char log_message_buffer[HCI_DUMP_MAX_MESSAGE_LEN];

// Format and send HCI packet in H4 format
// Enhanced version that preserves direction information
static void hci_dump_epm_embedded_async_uart_packet(uint8_t packet_type, uint8_t in, uint8_t * packet, uint16_t len){
    // Skip packets that are too large (reserve 1 byte for H4 type only)
    if (len + 1 > H4_PACKET_BUFFER_SIZE) {
        return;
    }
    
    // Convert BTstack packet type to H4 format
    uint8_t h4_type;
    bool should_forward = true;
    
    switch (packet_type){
        case HCI_COMMAND_DATA_PACKET:
            h4_type = HCI_COMMAND_DATA_PACKET;    // 0x01
            break;
        case HCI_EVENT_PACKET:
            h4_type = HCI_EVENT_PACKET;           // 0x04
            break;
        case HCI_ACL_DATA_PACKET:
#ifdef HCI_DUMP_STDOUT_MAX_SIZE_ACL
            if (len > HCI_DUMP_STDOUT_MAX_SIZE_ACL){
                return;
            }
#endif
            h4_type = HCI_ACL_DATA_PACKET;        // 0x02
            break;
        case HCI_SCO_DATA_PACKET:
#ifdef HCI_DUMP_STDOUT_MAX_SIZE_SCO
            if (len > HCI_DUMP_STDOUT_MAX_SIZE_SCO){
                return;
            }
#endif
            h4_type = HCI_SCO_DATA_PACKET;        // 0x03
            break;
        case HCI_ISO_DATA_PACKET:
#ifdef HCI_DUMP_STDOUT_MAX_SIZE_ISO
            if (len > HCI_DUMP_STDOUT_MAX_SIZE_ISO){
                return;
            }
#endif
            h4_type = HCI_ISO_DATA_PACKET;        // 0x05
            break;
        case LOG_MESSAGE_PACKET:
            // Send log messages as text with special marker
            static const char log_prefix[] = "LOG: ";
            log_async_write(log_prefix, sizeof(log_prefix) - 1);
            log_async_write(packet, len);
            log_async_write("\n", 1);
            return;
        default:
            return;
    }
    
    if (!should_forward) {
        return;
    }
    
    // Build Standard H4 packet: [H4_TYPE][HCI_PACKET_DATA] (Wireshark compatible)
    h4_packet_buffer[0] = h4_type;
    memcpy(&h4_packet_buffer[1], packet, len);
    
    // Send standard H4 packet via async UART
    log_async_write(h4_packet_buffer, len + 1);
}

static void hci_dump_epm_embedded_async_uart_log_packet(uint8_t packet_type, uint8_t in, uint8_t *packet, uint16_t len) {
    hci_dump_epm_embedded_async_uart_packet(packet_type, in, packet, len);
}

int _log_message = 0;
static void hci_dump_epm_embedded_async_uart_log_message(int log_level, const char * format, va_list argptr){
    UNUSED(log_level);

    if (!_log_message) {
        // Avoid logging if not enabled
        return;
    }
    int len = vsnprintf(log_message_buffer, sizeof(log_message_buffer), format, argptr);
    if (len > 0 && len < (int)sizeof(log_message_buffer)) {
        hci_dump_epm_embedded_async_uart_log_packet(LOG_MESSAGE_PACKET, 0, (uint8_t*) log_message_buffer, len);
    }
}

const hci_dump_t * hci_dump_epm_embedded_async_uart_get_instance(void){
    static const hci_dump_t hci_dump_instance = {
        // void (*reset)(void);
        NULL,
        // void (*log_packet)(uint8_t packet_type, uint8_t in, uint8_t *packet, uint16_t len);
        &hci_dump_epm_embedded_async_uart_log_packet,
        // void (*log_message)(int log_level, const char * format, va_list argptr);
        &hci_dump_epm_embedded_async_uart_log_message,
    };
    log_async_write_init();  // Initialize async write for UART
    return &hci_dump_instance;
}
