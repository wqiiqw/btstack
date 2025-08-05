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

//#define _EPM_HCI_DUMP_FORMAT 
#define _H4_HCI_DUMP_FORMAT

#define CRC8_POLY 0x07  // CRC-8-ATM polynomial: x^8 + x^2 + x + 1

static uint8_t _crc8(const uint8_t *buf, size_t size)
{
    uint8_t crc = 0x00;

    for (size_t i = 0; i < size; ++i) {
        crc ^= buf[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ CRC8_POLY;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}


// Buffer for H4 packet formatting
#define H4_PACKET_BUFFER_SIZE 1024
static uint8_t h4_packet_buffer[H4_PACKET_BUFFER_SIZE];

static char log_message_buffer[HCI_DUMP_MAX_MESSAGE_LEN];

// Format and send HCI packet in H4 format
// Enhanced version that preserves direction information
static void hci_dump_h4_hci_async_uart_log_packet(uint8_t packet_type, uint8_t in, uint8_t * packet, uint16_t len){
    // Skip packets that are too large (reserve 1 byte for H4 type only)
    if (len + 1 > H4_PACKET_BUFFER_SIZE) {
        return;
    }
    
    switch (packet_type){
        case HCI_COMMAND_DATA_PACKET:
        case HCI_EVENT_PACKET:
            break;

        case HCI_ACL_DATA_PACKET:
#ifdef HCI_DUMP_STDOUT_MAX_SIZE_ACL
            if (len > HCI_DUMP_STDOUT_MAX_SIZE_ACL){
                return;
            }
#endif
            break;
        case HCI_SCO_DATA_PACKET:
#ifdef HCI_DUMP_STDOUT_MAX_SIZE_SCO
            if (len > HCI_DUMP_STDOUT_MAX_SIZE_SCO){
                return;
            }
#endif
            break;
        case HCI_ISO_DATA_PACKET:
#ifdef HCI_DUMP_STDOUT_MAX_SIZE_ISO
            if (len > HCI_DUMP_STDOUT_MAX_SIZE_ISO){
                return;
            }
#endif
            break;
        case LOG_MESSAGE_PACKET:
        default:
            return;
    }
    
    // Build Standard H4 packet: [H4_TYPE][HCI_PACKET_DATA] (Wireshark compatible)
    h4_packet_buffer[0] = packet_type;
    memcpy(&h4_packet_buffer[1], packet, len);
    
    // Send standard H4 packet via async UART
    log_async_write(h4_packet_buffer, len + 1);
}

static void hci_dump_epm_embedded_async_uart_log_packet(uint8_t packet_type, uint8_t in, uint8_t * packet, uint16_t len){
    // EPM HCI Log Data Format:
    // [SYNC_MAGIC][TYPE][LEN][PAYLOAD][CRC8]
    // 0x8EA5       0xC5  LEN  PAYLOAD  CRC8
    // 
    // PAYLOAD: [uint8_t packet_type][uint8_t in][uint8_t *packet][uint16_t len]
    
    // Calculate total payload size: packet_type(1) + in(1) + packet(len) + len(2)
    uint16_t payload_size = 1 + 1 + len + 2;
    
    // Calculate total EPM packet size: SYNC(2) + TYPE(1) + LEN(2) + PAYLOAD + CRC8(1)
    uint16_t total_size = 2 + 1 + 2 + payload_size + 1;
    
    // Skip packets that are too large
    if (total_size > H4_PACKET_BUFFER_SIZE) {
        return;
    }
    
    uint8_t * epm_packet = h4_packet_buffer;
    uint16_t offset = 0;
    
    // SYNC_MAGIC (0x8EA5 - little endian)
    epm_packet[offset++] = 0xA5;
    epm_packet[offset++] = 0x8E;
    
    // TYPE (0xC5)
    epm_packet[offset++] = 0xC5;
    
    // LEN (payload size - little endian)
    epm_packet[offset++] = payload_size & 0xFF;
    epm_packet[offset++] = (payload_size >> 8) & 0xFF;
    
    // Mark start of PAYLOAD for CRC calculation
    uint16_t payload_start = offset;
    
    // PAYLOAD
    // - packet_type
    epm_packet[offset++] = packet_type;
    // - in (direction)
    epm_packet[offset++] = in;
    // - packet data
    memcpy(&epm_packet[offset], packet, len);
    offset += len;
    // - len (little endian)
    epm_packet[offset++] = len & 0xFF;
    epm_packet[offset++] = (len >> 8) & 0xFF;
    
    // Calculate CRC8 over PAYLOAD only (excluding SYNC_MAGIC, TYPE, and LEN)
    uint8_t crc = _crc8(&epm_packet[payload_start], payload_size);
    epm_packet[offset++] = crc;
    
    // Send EPM packet via async UART
    log_async_write(epm_packet, offset);
}

#if defined(_H4_HCI_DUMP_FORMAT)
int _log_message = 0;
#elif defined(_EPM_HCI_DUMP_FORMAT)
int _log_message = 1; //1
#else
int _log_message = 0; 
#endif
static void hci_dump_epm_embedded_async_uart_log_message(int log_level, const char * format, va_list argptr){
    UNUSED(log_level);

    if (!_log_message) {
        // Avoid logging if not enabled
        return;
    }

    int len = vsnprintf(log_message_buffer, sizeof(log_message_buffer) - 2, format, argptr); // Reserve space for '\r\n'
    if (len > 0 && len < (int)(sizeof(log_message_buffer) - 2)) {
        log_message_buffer[len] = '\r';      // Add carriage return
        log_message_buffer[len + 1] = '\n';  // Add newline
        log_message_buffer[len + 2] = '\0';  // Null terminate the string
        log_async_write(log_message_buffer, len + 2);  // Include '\r\n' in the length
    }
}

const hci_dump_t * hci_dump_epm_embedded_async_uart_get_instance(void){
    static const hci_dump_t hci_dump_instance = {
        // void (*reset)(void);
        NULL,
        // void (*log_packet)(uint8_t packet_type, uint8_t in, uint8_t *packet, uint16_t len);
#if defined(_H4_HCI_DUMP_FORMAT)
        &hci_dump_h4_hci_async_uart_log_packet,
#else
        &hci_dump_epm_embedded_async_uart_log_packet,
#endif
        // void (*log_message)(int log_level, const char * format, va_list argptr);
        &hci_dump_epm_embedded_async_uart_log_message,
    };
    log_async_write_init();  // Initialize async write for UART
    return &hci_dump_instance;
}
