#ifndef COM30_UART_H
#define COM30_UART_H

#include <stdint.h>
#include <stdbool.h>

// Configuration structure
typedef struct {
    const char* port_name;      // e.g., "COM30"
    uint32_t baudrate;          // e.g., 9600
    uint8_t flowcontrol;        // 0=none, 1=RTS/CTS
    uint8_t parity;             // 0=none, 1=even, 2=odd
} com30_uart_config_t;

// Callback function types
typedef void (*com30_data_received_callback_t)(uint8_t *data, uint16_t len);
typedef void (*com30_data_sent_callback_t)(void);
typedef void (*com30_error_callback_t)(int error_code);

// Error codes
#define COM30_ERROR_TIMEOUT     -1
#define COM30_ERROR_WRITE_FAIL  -2
#define COM30_ERROR_READ_FAIL   -3
#define COM30_ERROR_PORT_CLOSED -4

// Public API
int com30_uart_init(const com30_uart_config_t *config);
int com30_uart_open(void);
int com30_uart_close(void);
int com30_uart_send(const uint8_t *data, uint16_t len);
int com30_uart_receive(uint8_t *buffer, uint16_t len);
void com30_uart_set_callbacks(com30_data_received_callback_t received_cb,
                              com30_data_sent_callback_t sent_cb,
                              com30_error_callback_t error_cb);

// Status query
bool com30_uart_is_open(void);
int com30_uart_get_last_error(void);

#endif // COM30_UART_H