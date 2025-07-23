// com31_uart.c
#include "com31_uart.h"
#include "btstack_run_loop.h"
#include "btstack_debug.h"
#include <Windows.h>
#include <string.h>

// Configuration
#define COM31_TX_BUFFER_SIZE    1024    // Ring buffer size
#define COM31_TX_CHUNK_SIZE     64      // Max bytes per write operation

// Internal state
typedef struct {
    // Configuration
    com31_uart_config_t config;
    char port_name_buffer[32];
    
    // Windows handles
    HANDLE port_handle;
    OVERLAPPED write_overlapped;
    
    // BTstack integration
    btstack_data_source_t write_data_source;
    
    // Ring buffer for TX
    btstack_ring_buffer_t tx_ring_buffer;
    uint8_t tx_buffer_storage[COM31_TX_BUFFER_SIZE];
    
    // Current write operation
    uint8_t current_write_buffer[COM31_TX_CHUNK_SIZE];
    uint16_t current_write_len;
    bool write_operation_pending;
    
    // Callbacks
    com31_data_sent_callback_t sent_callback;
    com31_error_callback_t error_callback;
    
    // Status
    bool is_open;
    int last_error;
    
} com31_uart_state_t;

// Global state (single instance)
static com31_uart_state_t com31_state = {0};

// Forward declarations
static void com31_process_write_completion(btstack_data_source_t *ds, 
                                          btstack_data_source_callback_type_t callback_type);
static void com31_start_next_write_operation(void);
static void com31_handle_write_error(DWORD error);

// ============================================================================
// PUBLIC API IMPLEMENTATION
// ============================================================================

int com31_uart_init(const com31_uart_config_t *config) {
    if (!config || !config->port_name) {
        return -1;
    }
    
    // Clear state
    memset(&com31_state, 0, sizeof(com31_state));
    
    // Copy configuration
    com31_state.config = *config;
    strncpy_s(com31_state.port_name_buffer, sizeof(com31_state.port_name_buffer), 
              config->port_name, _TRUNCATE);
    com31_state.config.port_name = com31_state.port_name_buffer;
    
    // Initialize ring buffer
    btstack_ring_buffer_init(&com31_state.tx_ring_buffer, 
                            com31_state.tx_buffer_storage, 
                            COM31_TX_BUFFER_SIZE);
    
    // Initialize handles to invalid values
    com31_state.port_handle = INVALID_HANDLE_VALUE;
    
    log_info("COM31 UART initialized for port %s", config->port_name);
    return 0;
}

int com31_uart_open(void) {
    if (com31_state.is_open) {
        log_info("COM31 UART already open");
        return 0;
    }
    
    // Open COM port with overlapped I/O
    com31_state.port_handle = CreateFile(
        com31_state.config.port_name,
        GENERIC_WRITE,        // Only need write access
        0,                    // No sharing
        NULL,                 // Default security
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, // Enable overlapped I/O
        NULL
    );
    
    if (com31_state.port_handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        log_error("COM31: Failed to open port %s, error %lu", 
                  com31_state.config.port_name, error);
        com31_state.last_error = COM31_ERROR_PORT_CLOSED;
        return -1;
    }
    
    // Configure port settings
    DCB dcb;
    memset(&dcb, 0, sizeof(DCB));
    dcb.DCBlength = sizeof(DCB);
    
    if (!GetCommState(com31_state.port_handle, &dcb)) {
        log_error("COM31: Failed to get comm state");
        CloseHandle(com31_state.port_handle);
        com31_state.port_handle = INVALID_HANDLE_VALUE;
        return -1;
    }
    
    // Set serial parameters
    dcb.BaudRate = com31_state.config.baudrate;
    dcb.ByteSize = 8;
    dcb.Parity = com31_state.config.parity;
    dcb.StopBits = ONESTOPBIT;
    dcb.fOutxCtsFlow = com31_state.config.flowcontrol;
    dcb.fRtsControl = com31_state.config.flowcontrol ? 
                      RTS_CONTROL_HANDSHAKE : RTS_CONTROL_ENABLE;
    
    if (!SetCommState(com31_state.port_handle, &dcb)) {
        log_error("COM31: Failed to set comm state");
        CloseHandle(com31_state.port_handle);
        com31_state.port_handle = INVALID_HANDLE_VALUE;
        return -1;
    }
    
    // Create overlapped structure for write operations
    memset(&com31_state.write_overlapped, 0, sizeof(OVERLAPPED));
    com31_state.write_overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    if (!com31_state.write_overlapped.hEvent) {
        log_error("COM31: Failed to create write event");
        CloseHandle(com31_state.port_handle);
        com31_state.port_handle = INVALID_HANDLE_VALUE;
        return -1;
    }
    
    // Setup BTstack data source for write completion
    com31_state.write_data_source.source.handle = com31_state.write_overlapped.hEvent;
    btstack_run_loop_set_data_source_handler(&com31_state.write_data_source, 
                                             &com31_process_write_completion);
    
    // Add to BTstack run loop
    btstack_run_loop_add_data_source(&com31_state.write_data_source);
    
    com31_state.is_open = true;
    log_info("COM31 UART opened successfully");
    return 0;
}

int com31_uart_close(void) {
    if (!com31_state.is_open) {
        return 0;
    }
    
    com31_state.is_open = false;
    
    // Remove from BTstack run loop
    btstack_run_loop_remove_data_source(&com31_state.write_data_source);
    
    // Close Windows handles
    if (com31_state.write_overlapped.hEvent) {
        CloseHandle(com31_state.write_overlapped.hEvent);
        com31_state.write_overlapped.hEvent = NULL;
    }
    
    if (com31_state.port_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(com31_state.port_handle);
        com31_state.port_handle = INVALID_HANDLE_VALUE;
    }
    
    // Clear ring buffer
    btstack_ring_buffer_reset(&com31_state.tx_ring_buffer);
    com31_state.write_operation_pending = false;
    
    log_info("COM31 UART closed");
    return 0;
}

int com31_uart_send(const uint8_t *data, uint16_t len) {
    if (!com31_state.is_open || !data || len == 0) {
        return -1;
    }
    
    // Check if ring buffer has space
    uint32_t free_space = btstack_ring_buffer_bytes_free(&com31_state.tx_ring_buffer);
    if (free_space < len) {
        log_debug("COM31: TX buffer full, free=%lu, needed=%u", free_space, len);
        com31_state.last_error = COM31_ERROR_BUFFER_FULL;
        return -1;
    }
    
    // Add data to ring buffer - CORRECTED: returns int, takes 3 params
    int bytes_stored = btstack_ring_buffer_write(&com31_state.tx_ring_buffer, 
                                                (uint8_t*)data,  // Cast away const
                                                len);
    
    if (bytes_stored != (int)len) {
        log_error("COM31: Ring buffer write failed, stored=%d, expected=%u", 
                  bytes_stored, len);
        return -1;
    }
    
    log_debug("COM31: Queued %u bytes for transmission", len);
    
    // Start transmission if not already active
    if (!com31_state.write_operation_pending) {
        com31_start_next_write_operation();
    }
    
    return 0;
}

void com31_uart_set_callbacks(com31_data_sent_callback_t sent_cb,
                              com31_error_callback_t error_cb) {
    com31_state.sent_callback = sent_cb;
    com31_state.error_callback = error_cb;
}

bool com31_uart_is_open(void) {
    return com31_state.is_open;
}

int com31_uart_get_last_error(void) {
    return com31_state.last_error;
}

uint16_t com31_uart_get_tx_buffer_free_space(void) {
    if (!com31_state.is_open) {
        return 0;
    }
    
    uint32_t free = btstack_ring_buffer_bytes_free(&com31_state.tx_ring_buffer);
    return (free > UINT16_MAX) ? UINT16_MAX : (uint16_t)free;
}

// ============================================================================
// INTERNAL IMPLEMENTATION
// ============================================================================

static void com31_start_next_write_operation(void) {
    // Don't start if already pending or no data
    if (com31_state.write_operation_pending || 
        btstack_ring_buffer_bytes_available(&com31_state.tx_ring_buffer) == 0) {
        return;
    }
    
    // Read chunk from ring buffer - CORRECTED API USAGE
    uint32_t available = btstack_ring_buffer_bytes_available(&com31_state.tx_ring_buffer);
    uint16_t chunk_size = (available > COM31_TX_CHUNK_SIZE) ? 
                          COM31_TX_CHUNK_SIZE : (uint16_t)available;
    
    // Correct BTstack ring buffer read API - 4 parameters
    uint32_t bytes_read;
    btstack_ring_buffer_read(&com31_state.tx_ring_buffer,
                            com31_state.current_write_buffer,
                            chunk_size,
                            &bytes_read);  // Pass pointer to receive bytes read
    
    if (bytes_read == 0) {
        log_debug("COM31: No data to send");
        return;
    }
    
    com31_state.current_write_len = (uint16_t)bytes_read;
    
    log_debug("COM31: Starting write operation, %u bytes", com31_state.current_write_len);
    
    // Start async write
    DWORD bytes_written;
    BOOL ok = WriteFile(
        com31_state.port_handle,
        com31_state.current_write_buffer,
        com31_state.current_write_len,
        &bytes_written,
        &com31_state.write_overlapped
    );
    
    if (ok) {
        // Immediate completion (rare for serial ports)
        log_debug("COM31: Write completed immediately, %lu bytes", bytes_written);
        
        // Check if ring buffer is now empty
        if (btstack_ring_buffer_bytes_available(&com31_state.tx_ring_buffer) == 0) {
            // All data sent, notify application
            if (com31_state.sent_callback) {
                com31_state.sent_callback();
            }
        } else {
            // More data available, continue sending
            com31_start_next_write_operation();
        }
        return;
    }
    
    DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING) {
        // Normal async operation - mark as pending and wait for completion
        com31_state.write_operation_pending = true;
        btstack_run_loop_enable_data_source_callbacks(&com31_state.write_data_source, 
                                                       DATA_SOURCE_CALLBACK_WRITE);
        log_debug("COM31: Write operation pending");
    } else {
        // Write error - put data back in ring buffer
        log_error("COM31: WriteFile failed, error %lu", error);
        
        // Try to put data back (best effort) - CORRECTED: 3 parameters, returns int
        int bytes_restored = btstack_ring_buffer_write(&com31_state.tx_ring_buffer,
                                                      com31_state.current_write_buffer,
                                                      com31_state.current_write_len);
        
        if (bytes_restored != (int)com31_state.current_write_len) {
            log_error("COM31: Failed to restore %u bytes to ring buffer, only restored %d", 
                      com31_state.current_write_len, bytes_restored);
        }
        
        com31_handle_write_error(error);
    }
}

static void com31_process_write_completion(btstack_data_source_t *ds, 
                                          btstack_data_source_callback_type_t callback_type) {
    // Disable write callbacks
    btstack_run_loop_disable_data_source_callbacks(ds, DATA_SOURCE_CALLBACK_WRITE);
    
    DWORD bytes_written;
    BOOL ok = GetOverlappedResult(com31_state.port_handle, 
                                  &com31_state.write_overlapped, 
                                  &bytes_written, 
                                  FALSE);
    
    com31_state.write_operation_pending = false;
    
    if (!ok) {
        DWORD error = GetLastError();
        if (error == ERROR_IO_INCOMPLETE) {
            // Still in progress - re-enable callbacks
            com31_state.write_operation_pending = true;
            btstack_run_loop_enable_data_source_callbacks(ds, DATA_SOURCE_CALLBACK_WRITE);
            return;
        } else {
            // Write error - put data back in ring buffer
            log_error("COM31: Write completion error %lu", error);
            
            // Try to put data back (best effort) - CORRECTED: 3 parameters, returns int
            int bytes_restored = btstack_ring_buffer_write(&com31_state.tx_ring_buffer,
                                                          com31_state.current_write_buffer,
                                                          com31_state.current_write_len);
            
            if (bytes_restored != (int)com31_state.current_write_len) {
                log_error("COM31: Failed to restore %u bytes to ring buffer, only restored %d", 
                          com31_state.current_write_len, bytes_restored);
            }
            
            com31_handle_write_error(error);
            return;
        }
    }
    
    log_debug("COM31: Write completed, %lu bytes sent", bytes_written);
    
    // Check if we have more data to send
    if (btstack_ring_buffer_bytes_available(&com31_state.tx_ring_buffer) > 0) {
        // More data available, continue sending
        com31_start_next_write_operation();
    } else {
        // All data sent, notify application
        log_debug("COM31: All queued data transmitted");
        if (com31_state.sent_callback) {
            com31_state.sent_callback();
        }
    }
}

static void com31_handle_write_error(DWORD error) {
    com31_state.last_error = COM31_ERROR_WRITE_FAIL;
    
    if (error == ERROR_DEVICE_NOT_CONNECTED) {
        log_error("COM31: Device disconnected");
        com31_uart_close();
    }
    
    if (com31_state.error_callback) {
        com31_state.error_callback(com31_state.last_error);
    }
}