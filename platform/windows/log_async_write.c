#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "btstack_ring_buffer.h"
#include "btstack_run_loop.h"
#include "btstack_debug.h"
#include "log_async_write.h"
#include "btstack_uart.h"
#include "btstack_uart_block.h"

#define TSLOG_STORAGE_SIZE                       4096
#define TSLOG_FLUSH_CHUNK_SIZE                   256   // Max bytes to send per task iteration
// Flush interval when data is pending (milliseconds):
//   0ms  = No delay - maximum speed but higher CPU usage, risk of busy loops
//   1ms  = Real-time debugging - low latency, good for interactive debugging  
//   5ms  = High throughput - balance between speed and efficiency
//  10ms  = Default balanced - matches UART transmission time (~22ms for 256 bytes at 115200 baud)
//  50ms  = Low power mode - minimal CPU impact but higher latency, risk of buffer overflow
#define TSLOG_FLUSH_INTERVAL_MS                  10

// Ring buffer and storage
static btstack_ring_buffer_t _tslog_ringbuffer;
static uint32_t _tslog_storage[TSLOG_STORAGE_SIZE/4];

// Statistics and state
volatile uint32_t _tslog_lost_count = 0;
static bool _tslog_initialized = false;

// BTstack timer for periodic flush
static btstack_timer_source_t _tslog_flush_timer;
static bool _tslog_flush_active = false;

// Forward declarations
static void log_async_flush_task(btstack_timer_source_t *ts);
static void log_async_schedule_flush(void);

// UART driver instance
static const btstack_uart_block_t * uart_driver = NULL;

// Flush task - runs in BTstack main loop context
static void log_async_flush_task(btstack_timer_source_t *ts) {
    UNUSED(ts);

    if (!_tslog_initialized) {
        _tslog_flush_active = false;
        return;
    }

    // UART readiness: BTstack UART block driver does not provide is_open().
    // Assume UART is ready after successful init/open.

    // Get available data in ring buffer
    uint32_t bytes_available = btstack_ring_buffer_bytes_available(&_tslog_ringbuffer);
    if (bytes_available == 0) {
        _tslog_flush_active = false;
        printf("log_async: No data available, flush inactive\n");
        return;
    }

    printf("log_async: Flushing %lu bytes available\n", (unsigned long)bytes_available);

    uint32_t bytes_to_send = (bytes_available > TSLOG_FLUSH_CHUNK_SIZE) ? TSLOG_FLUSH_CHUNK_SIZE : bytes_available;
    uint8_t flush_buffer[TSLOG_FLUSH_CHUNK_SIZE];
    uint32_t bytes_read = 0;
    btstack_ring_buffer_read(&_tslog_ringbuffer, flush_buffer, bytes_to_send, &bytes_read);

    printf("log_async: Read %lu bytes from ring buffer\n", (unsigned long)bytes_read);

    if (bytes_read > 0) {
        // Send to UART (blocking)
        // btstack_uart_block_windows send_block returns void, no error code
        uart_driver->send_block(flush_buffer, (uint16_t)bytes_read);
        printf("log_async: Sent %lu bytes to UART\n", (unsigned long)bytes_read);
    }

    bytes_available = btstack_ring_buffer_bytes_available(&_tslog_ringbuffer);
    if (bytes_available > 0) {
        printf("log_async: %lu bytes remaining, rescheduling\n", (unsigned long)bytes_available);
        btstack_run_loop_set_timer(&_tslog_flush_timer, TSLOG_FLUSH_INTERVAL_MS);
        btstack_run_loop_set_timer_handler(&_tslog_flush_timer, log_async_flush_task);
        btstack_run_loop_add_timer(&_tslog_flush_timer);
    } else {
        _tslog_flush_active = false;
        printf("log_async: All data flushed, flush inactive\n");
    }
}

// Schedule flush task to run in BTstack main loop
static void log_async_schedule_flush(void) {
    if (!_tslog_initialized) {
        return;
    }
    
    if (_tslog_flush_active) {
        // Flush already scheduled, don't schedule again
        return;
    }
    
    printf("log_async: Scheduling flush task\n");
    
    // Set timer to run as soon as possible (0ms delay)
    btstack_run_loop_set_timer(&_tslog_flush_timer, 0);
    btstack_run_loop_set_timer_handler(&_tslog_flush_timer, log_async_flush_task);
    btstack_run_loop_add_timer(&_tslog_flush_timer);
    
    _tslog_flush_active = true;
}

// Public API implementation
void log_async_write_init(void) {
    if (_tslog_initialized) {
        return;
    }

    btstack_ring_buffer_init(&_tslog_ringbuffer, (uint8_t*)_tslog_storage, TSLOG_STORAGE_SIZE);
    _tslog_lost_count = 0;
    _tslog_flush_active = false;
    _tslog_initialized = true;

    printf("log_async: Initialized with %d byte ring buffer\n", TSLOG_STORAGE_SIZE);

    // Use BTstack UART block driver for Windows
    uart_driver = btstack_uart_block_windows_instance();
    btstack_uart_config_t uart_config = {
        .baudrate = 500000,
        .flowcontrol = 1,
        .device_name = "COM8",
        .parity = 0
    };

    if (uart_driver->init(&uart_config) != 0) {
        printf("Failed to initialize UART\n");
        return;
    }
    if (uart_driver->open() != 0) {
        printf("Failed to open UART\n");
        return;
    }
    printf("BTstack with %s UART starting...\n", uart_config.device_name);
}

void log_async_write(const void * data, int size) {
    if (!_tslog_initialized || !data || size <= 0) {
        return;
    }
    
    // Check available space in ring buffer
    uint32_t bytes_free = btstack_ring_buffer_bytes_free(&_tslog_ringbuffer);
    if ((uint32_t)size > bytes_free) {
        // Not enough space - record lost data and return
        _tslog_lost_count += (uint32_t)size;
        printf("log_async: Buffer full, %d bytes lost (free: %lu)\n", size, (unsigned long)bytes_free);
        return;
    }
    
    // Copy data to ring buffer (non-blocking operation)
    int result = btstack_ring_buffer_write(&_tslog_ringbuffer, (uint8_t*)data, (uint32_t)size);
    
    if (result != 0) {
        // Write failed - count as lost
        _tslog_lost_count += (uint32_t)size;
        printf("log_async: Ring buffer write failed with error %d, %d bytes lost\n", result, size);
        return;
    }
    
    printf("log_async: Wrote %d bytes to ring buffer\n", size);
    
    // Data successfully written, schedule flush task
    log_async_schedule_flush();
}

// Optional: Get statistics
uint32_t log_async_get_lost_count(void) {
    return _tslog_lost_count;
}

// Optional cleanup function
void log_async_write_deinit(void) {
    if (!_tslog_initialized) {
        return;
    }
    
    // Remove timer if active
    if (_tslog_flush_active) {
        btstack_run_loop_remove_timer(&_tslog_flush_timer);
        _tslog_flush_active = false;
    }
    
    // Final flush attempt
    log_async_flush_task(&_tslog_flush_timer);

    // Cleanup
    printf("log_async: Closing UART\n");
    if (uart_driver && uart_driver->close) {
        uart_driver->close();
    }

    _tslog_initialized = false;
    printf("log_async: Deinitialized, %lu bytes lost total\n", (unsigned long)_tslog_lost_count);
}
