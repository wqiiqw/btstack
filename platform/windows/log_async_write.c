#include <stdint.h>
#include <string.h>
#include "btstack_ring_buffer.h"
#include "btstack_run_loop.h"
#include "btstack_debug.h"
#include "log_async_write.h"
#include "com30_uart.h"

#define TSLOG_STORAGE_SIZE                       4096
#define TSLOG_FLUSH_CHUNK_SIZE                   256   // Max bytes to send per task iteration

// Ring buffer and storage
static btstack_ring_buffer_t _tslog_ringbuffer;
static uint32_t _tslog_storage[TSLOG_STORAGE_SIZE/4];

// Statistics and state
static uint32_t _tslog_lost_count = 0;
static bool _tslog_initialized = false;

// BTstack timer for periodic flush
static btstack_timer_source_t _tslog_flush_timer;
static bool _tslog_flush_active = false;

// Forward declarations
static void log_async_flush_task(btstack_timer_source_t *ts);
static void log_async_schedule_flush(void);

// Flush task - runs in BTstack main loop context
static void log_async_flush_task(btstack_timer_source_t *ts) {
    UNUSED(ts);
    
    if (!_tslog_initialized) {
        return;
    }
    
    // Check if COM30 is ready for transmission
    if (!com30_uart_is_open()) {
        // COM30 not ready, reschedule for later
        log_async_schedule_flush();
        return;
    }
    
    // Get available data in ring buffer
    uint32_t bytes_available = btstack_ring_buffer_bytes_available(&_tslog_ringbuffer);
    if (bytes_available == 0) {
        // No data to flush, mark flush as inactive
        _tslog_flush_active = false;
        return;
    }
    
    // Determine how much to send this iteration
    uint32_t bytes_to_send = (bytes_available > TSLOG_FLUSH_CHUNK_SIZE) ? 
                             TSLOG_FLUSH_CHUNK_SIZE : bytes_available;
    
    // Allocate temporary buffer on stack
    uint8_t flush_buffer[TSLOG_FLUSH_CHUNK_SIZE];
    
    // Read data from ring buffer - API: void btstack_ring_buffer_read(buffer, dest, length, *bytes_read)
    uint32_t bytes_read = 0;
    btstack_ring_buffer_read(&_tslog_ringbuffer, flush_buffer, bytes_to_send, &bytes_read);
    
    if (bytes_read > 0) {
        // Send to COM30 (non-blocking)
        int result = com30_uart_send(flush_buffer, (uint16_t)bytes_read);
        if (result != 0) {
            // Send failed - count as lost data
            _tslog_lost_count += bytes_read;
            log_debug("log_async: COM30 send failed, %lu bytes lost", (unsigned long)bytes_read);
        }
    }
    
    // Check if more data needs to be flushed
    bytes_available = btstack_ring_buffer_bytes_available(&_tslog_ringbuffer);
    if (bytes_available > 0) {
        // More data available, reschedule immediately
        log_async_schedule_flush();
    } else {
        // All data flushed, mark flush as inactive
        _tslog_flush_active = false;
    }
}

// Schedule flush task to run in BTstack main loop
static void log_async_schedule_flush(void) {
    if (!_tslog_initialized || _tslog_flush_active) {
        return;
    }
    
    // Set timer to run as soon as possible (0ms delay)
    btstack_run_loop_set_timer(&_tslog_flush_timer, 0);
    btstack_run_loop_set_timer_handler(&_tslog_flush_timer, log_async_flush_task);
    btstack_run_loop_add_timer(&_tslog_flush_timer);
    
    _tslog_flush_active = true;
}

// Public API implementation
void log_async_write_init(void) {
    if (_tslog_initialized) {
        return; // Already initialized
    }
    
    // Initialize ring buffer
    btstack_ring_buffer_init(&_tslog_ringbuffer, (uint8_t*)_tslog_storage, TSLOG_STORAGE_SIZE);
    
    // Reset statistics
    _tslog_lost_count = 0;
    _tslog_flush_active = false;
    
    // Mark as initialized
    _tslog_initialized = true;
    
    log_info("log_async: Initialized with %d byte ring buffer", TSLOG_STORAGE_SIZE);
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
        return;
    }
    
    // Copy data to ring buffer (non-blocking operation)
    // API: int btstack_ring_buffer_write(buffer, src, length) - returns 0 if ok, error code if failed
    int result = btstack_ring_buffer_write(&_tslog_ringbuffer, (uint8_t*)data, (uint32_t)size);
    
    if (result != 0) {
        // Write failed - count as lost (this shouldn't happen if we checked free space correctly)
        _tslog_lost_count += (uint32_t)size;
        log_debug("log_async: Ring buffer write failed with error %d, %d bytes lost", result, size);
        return;
    }
    
    // Data successfully written, schedule flush task
    log_async_schedule_flush();
}

// Optional: Get statistics
uint32_t log_async_get_lost_count(void) {
    return _tslog_lost_count;
}

uint32_t log_async_get_buffer_usage(void) {
    if (!_tslog_initialized) {
        return 0;
    }
    return btstack_ring_buffer_bytes_available(&_tslog_ringbuffer);
}

uint32_t log_async_get_buffer_free(void) {
    if (!_tslog_initialized) {
        return 0;
    }
    return btstack_ring_buffer_bytes_free(&_tslog_ringbuffer);
}

void log_async_reset_stats(void) {
    _tslog_lost_count = 0;
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
    
    // Mark as uninitialized
    _tslog_initialized = false;
    
    log_info("log_async: Deinitialized, %lu bytes lost total", (unsigned long)_tslog_lost_count);
}
