// com31_uart.h
#ifndef COM31_UART_H
#define COM31_UART_H

#include <stdint.h>
#include <stdbool.h>
#include "btstack_ring_buffer.h"

// Configuration
typedef struct {
    const char *port_name;
    uint32_t baudrate;
    uint8_t parity;
    bool flowcontrol;
} com31_uart_config_t;

// Error codes
#define COM31_ERROR_NONE           0
#define COM31_ERROR_PORT_CLOSED   -1
#define COM31_ERROR_WRITE_FAIL    -2
#define COM31_ERROR_BUFFER_FULL   -3

// Callbacks
typedef void (*com31_data_sent_callback_t)(void);
typedef void (*com31_error_callback_t)(int error);

// Public API
int com31_uart_init(const com31_uart_config_t *config);
int com31_uart_open(void);
int com31_uart_close(void);
int com31_uart_send(const uint8_t *data, uint16_t len);
void com31_uart_set_callbacks(com31_data_sent_callback_t sent_cb, 
                              com31_error_callback_t error_cb);
bool com31_uart_is_open(void);
int com31_uart_get_last_error(void);
uint16_t com31_uart_get_tx_buffer_free_space(void);

#endif