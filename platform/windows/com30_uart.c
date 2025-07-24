#include "com30_uart.h"
#include "btstack_run_loop.h"
#include "btstack_debug.h"
#include <Windows.h>
#include <string.h>

// Internal state structure
typedef struct {
    // Configuration
    com30_uart_config_t config;
    char port_name_buffer[32];
    
    // Windows handles
    HANDLE port_handle;
    OVERLAPPED read_overlapped;
    OVERLAPPED write_overlapped;
    
    // BTstack integration
    btstack_data_source_t read_data_source;
    btstack_data_source_t write_data_source;
    
    // Transfer state
    uint8_t *rx_buffer;
    uint16_t rx_expected_len;
    uint16_t rx_received_len;
    
    const uint8_t *tx_data;
    uint16_t tx_len;
    uint16_t tx_sent;
    
    // Callbacks
    com30_data_received_callback_t received_callback;
    com30_data_sent_callback_t sent_callback;
    com30_error_callback_t error_callback;
    
    // Status
    bool is_open;
    int last_error;
    
} com30_uart_state_t;

// Global state (single instance)
static com30_uart_state_t com30_state = {0};

// Forward declarations
static void com30_process_read(btstack_data_source_t *ds, btstack_data_source_callback_type_t callback_type);
static void com30_process_write(btstack_data_source_t *ds, btstack_data_source_callback_type_t callback_type);
static void com30_start_read_operation(void);
static void com30_start_write_operation(void);

// ============================================================================
// PUBLIC API IMPLEMENTATION
// ============================================================================

int com30_uart_init(const com30_uart_config_t *config) {
    if (!config || !config->port_name) {
        return -1;
    }
    
    // Clear state
    memset(&com30_state, 0, sizeof(com30_state));
    
    // Copy configuration
    com30_state.config = *config;
    strncpy_s(com30_state.port_name_buffer, sizeof(com30_state.port_name_buffer), config->port_name, _TRUNCATE);
    com30_state.config.port_name = com30_state.port_name_buffer;
    
    // Initialize handles to invalid values
    com30_state.port_handle = INVALID_HANDLE_VALUE;
    
    log_info("COM30 UART initialized for port %s", config->port_name);
    return 0;
}

int com30_uart_open(void) {
    if (com30_state.is_open) {
        log_info("COM30 UART already open");
        return 0;
    }
    
    // Open COM port with overlapped I/O
    com30_state.port_handle = CreateFile(
        com30_state.config.port_name,
        GENERIC_READ | GENERIC_WRITE,
        0,                    // No sharing
        NULL,                 // Default security
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, // Enable overlapped I/O
        NULL
    );
    
    if (com30_state.port_handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        log_error("COM30: Failed to open port %s, error %lu", com30_state.config.port_name, error);
        com30_state.last_error = COM30_ERROR_PORT_CLOSED;
        return -1;
    }
    
    // Configure port settings
    DCB dcb;
    memset(&dcb, 0, sizeof(DCB));
    dcb.DCBlength = sizeof(DCB);
    
    if (!GetCommState(com30_state.port_handle, &dcb)) {
        log_error("COM30: Failed to get comm state");
        CloseHandle(com30_state.port_handle);
        com30_state.port_handle = INVALID_HANDLE_VALUE;
        return -1;
    }
    
    // Set 8-N-1, baudrate, flow control
    dcb.BaudRate = com30_state.config.baudrate;
    dcb.ByteSize = 8;
    dcb.Parity = com30_state.config.parity;
    dcb.StopBits = ONESTOPBIT;
    dcb.fOutxCtsFlow = com30_state.config.flowcontrol;
    dcb.fRtsControl = com30_state.config.flowcontrol ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_ENABLE;
    
    if (!SetCommState(com30_state.port_handle, &dcb)) {
        log_error("COM30: Failed to set comm state");
        CloseHandle(com30_state.port_handle);
        com30_state.port_handle = INVALID_HANDLE_VALUE;
        return -1;
    }
    
    // Create overlapped structures
    memset(&com30_state.read_overlapped, 0, sizeof(OVERLAPPED));
    memset(&com30_state.write_overlapped, 0, sizeof(OVERLAPPED));
    
    com30_state.read_overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    com30_state.write_overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    if (!com30_state.read_overlapped.hEvent || !com30_state.write_overlapped.hEvent) {
        log_error("COM30: Failed to create events");
        com30_uart_close();
        return -1;
    }
    
    // Setup BTstack data sources
    com30_state.read_data_source.source.handle = com30_state.read_overlapped.hEvent;
    com30_state.write_data_source.source.handle = com30_state.write_overlapped.hEvent;
    
    btstack_run_loop_set_data_source_handler(&com30_state.read_data_source, &com30_process_read);
    btstack_run_loop_set_data_source_handler(&com30_state.write_data_source, &com30_process_write);
    
    // Add to BTstack run loop
    btstack_run_loop_add_data_source(&com30_state.read_data_source);
    btstack_run_loop_add_data_source(&com30_state.write_data_source);
    
    com30_state.is_open = true;
    log_info("COM30 UART opened successfully");
    return 0;
}

int com30_uart_close(void) {
    if (!com30_state.is_open) {
        return 0;
    }
    
    com30_state.is_open = false;
    
    // Remove from BTstack run loop
    btstack_run_loop_remove_data_source(&com30_state.read_data_source);
    btstack_run_loop_remove_data_source(&com30_state.write_data_source);
    
    // Close Windows handles
    if (com30_state.read_overlapped.hEvent) {
        CloseHandle(com30_state.read_overlapped.hEvent);
        com30_state.read_overlapped.hEvent = NULL;
    }
    
    if (com30_state.write_overlapped.hEvent) {
        CloseHandle(com30_state.write_overlapped.hEvent);
        com30_state.write_overlapped.hEvent = NULL;
    }
    
    if (com30_state.port_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(com30_state.port_handle);
        com30_state.port_handle = INVALID_HANDLE_VALUE;
    }
    
    log_info("COM30 UART closed");
    return 0;
}

int com30_uart_send(const uint8_t *data, uint16_t len) {
    if (!com30_state.is_open || !data || len == 0) {
        return -1;
    }
    
    // Store write parameters
    com30_state.tx_data = data;
    com30_state.tx_len = len;
    com30_state.tx_sent = 0;
    
    // Start write operation
    com30_start_write_operation();
    return 0;
}

int com30_uart_receive(uint8_t *buffer, uint16_t len) {
    if (!com30_state.is_open || !buffer || len == 0) {
        return -1;
    }
    
    // Store read parameters
    com30_state.rx_buffer = buffer;
    com30_state.rx_expected_len = len;
    com30_state.rx_received_len = 0;
    
    // Start read operation
    com30_start_read_operation();
    return 0;
}

void com30_uart_set_callbacks(com30_data_received_callback_t received_cb,
                              com30_data_sent_callback_t sent_cb,
                              com30_error_callback_t error_cb) {
    com30_state.received_callback = received_cb;
    com30_state.sent_callback = sent_cb;
    com30_state.error_callback = error_cb;
}

bool com30_uart_is_open(void) {
    return com30_state.is_open;
}

int com30_uart_get_last_error(void) {
    return com30_state.last_error;
}

// ============================================================================
// INTERNAL IMPLEMENTATION
// ============================================================================

static void com30_start_write_operation(void) {
    DWORD bytes_written;
    BOOL ok = WriteFile(
        com30_state.port_handle,
        com30_state.tx_data + com30_state.tx_sent,
        com30_state.tx_len - com30_state.tx_sent,
        &bytes_written,
        &com30_state.write_overlapped
    );
    
    if (ok) {
        // Write completed immediately (rare)
        if (bytes_written > UINT16_MAX) {
            log_error("COM30: Write size %lu exceeds uint16_t range", bytes_written);
            return;
        }
        com30_state.tx_sent += (uint16_t)bytes_written;
        
        if (com30_state.tx_sent >= com30_state.tx_len) {
            // Complete write
            if (com30_state.sent_callback) {
                com30_state.sent_callback();
            }
        } else {
            // Partial write - continue
            com30_start_write_operation();
        }
        return;
    }
    
    DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING) {
        // Normal async operation - wait for completion
        btstack_run_loop_enable_data_source_callbacks(&com30_state.write_data_source, 
                                                       DATA_SOURCE_CALLBACK_WRITE);
    } else {
        // Write error
        log_error("COM30: Write error %lu", error);
        com30_state.last_error = COM30_ERROR_WRITE_FAIL;
        if (com30_state.error_callback) {
            com30_state.error_callback(COM30_ERROR_WRITE_FAIL);
        }
    }
}

static void com30_start_read_operation(void) {
    DWORD bytes_read;
    BOOL ok = ReadFile(
        com30_state.port_handle,
        com30_state.rx_buffer + com30_state.rx_received_len,
        com30_state.rx_expected_len - com30_state.rx_received_len,
        &bytes_read,
        &com30_state.read_overlapped
    );
    
    if (ok) {
        // Read completed immediately (rare)
        com30_state.rx_received_len += (uint16_t)bytes_read;
        
        if (com30_state.rx_received_len >= com30_state.rx_expected_len) {
            // Complete read
            if (com30_state.received_callback) {
                com30_state.received_callback(com30_state.rx_buffer, com30_state.rx_received_len);
            }
        } else {
            // Partial read - continue
            com30_start_read_operation();
        }
        return;
    }
    
    DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING) {
        // Normal async operation - wait for completion
        btstack_run_loop_enable_data_source_callbacks(&com30_state.read_data_source, 
                                                       DATA_SOURCE_CALLBACK_READ);
    } else {
        // Read error
        log_error("COM30: Read error %lu", error);
        com30_state.last_error = COM30_ERROR_READ_FAIL;
        if (com30_state.error_callback) {
            com30_state.error_callback(COM30_ERROR_READ_FAIL);
        }
    }
}

static void com30_process_write(btstack_data_source_t *ds, btstack_data_source_callback_type_t callback_type) {
    // Disable write callbacks
    btstack_run_loop_disable_data_source_callbacks(ds, DATA_SOURCE_CALLBACK_WRITE);
    
    DWORD bytes_written;
    BOOL ok = GetOverlappedResult(com30_state.port_handle, 
                                  &com30_state.write_overlapped, 
                                  &bytes_written, 
                                  FALSE);
    
    if (!ok) {
        DWORD error = GetLastError();
        if (error == ERROR_IO_INCOMPLETE) {
            // Still in progress - re-enable callbacks
            btstack_run_loop_enable_data_source_callbacks(ds, DATA_SOURCE_CALLBACK_WRITE);
            return;
        } else {
            // Write error
            log_error("COM30: Write completion error %lu", error);
            com30_state.last_error = COM30_ERROR_WRITE_FAIL;
            if (com30_state.error_callback) {
                com30_state.error_callback(COM30_ERROR_WRITE_FAIL);
            }
            return;
        }
    }
    
    // Update sent count
    com30_state.tx_sent += (uint16_t)bytes_written;
    
    if (com30_state.tx_sent >= com30_state.tx_len) {
        // Complete write
        log_debug("COM30: Write completed, %u bytes sent", com30_state.tx_len);
        if (com30_state.sent_callback) {
            com30_state.sent_callback();
        }
    } else {
        // Partial write - continue with remaining data
        log_debug("COM30: Partial write, %lu of %u bytes sent, continuing", 
                  com30_state.tx_sent, com30_state.tx_len);
        com30_start_write_operation();
    }
}

static void com30_process_read(btstack_data_source_t *ds, btstack_data_source_callback_type_t callback_type) {
    // Disable read callbacks
    btstack_run_loop_disable_data_source_callbacks(ds, DATA_SOURCE_CALLBACK_READ);
    
    DWORD bytes_read;
    BOOL ok = GetOverlappedResult(com30_state.port_handle, 
                                  &com30_state.read_overlapped, 
                                  &bytes_read, 
                                  FALSE);
    
    if (!ok) {
        DWORD error = GetLastError();
        if (error == ERROR_IO_INCOMPLETE) {
            // Still in progress - re-enable callbacks
            btstack_run_loop_enable_data_source_callbacks(ds, DATA_SOURCE_CALLBACK_READ);
            return;
        } else {
            // Read error
            log_error("COM30: Read completion error %lu", error);
            com30_state.last_error = COM30_ERROR_READ_FAIL;
            if (com30_state.error_callback) {
                com30_state.error_callback(COM30_ERROR_READ_FAIL);
            }
            return;
        }
    }
    
    // Update received count
    com30_state.rx_received_len += (uint16_t)bytes_read;
    
    if (com30_state.rx_received_len >= com30_state.rx_expected_len) {
        // Complete read
        log_debug("COM30: Read completed, %u bytes received", com30_state.rx_received_len);
        if (com30_state.received_callback) {
            com30_state.received_callback(com30_state.rx_buffer, com30_state.rx_received_len);
        }
    } else {
        // Partial read - continue with remaining data
        log_debug("COM30: Partial read, %u of %u bytes received, continuing", 
                  com30_state.rx_received_len, com30_state.rx_expected_len);
        com30_start_read_operation();
    }
}