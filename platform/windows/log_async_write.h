#ifndef _LOG_ASYNC_WRITE_H
#define _LOG_ASYNC_WRITE_H
#include <stdint.h>

/*
 * Design Caveat:
 * ----------------
 * The log_async_write module uses a single FIFO ring buffer for all log data.
 * Each call to log_async_write() must provide a complete, self-contained message
 * (e.g., a full HCI packet or a full log string). If a logical message is split
 * across multiple log_async_write() calls, the output stream may interleave
 * bytes from different messages. To guarantee message integrity, always write
 * complete messages in a single
*/

// Core API
void log_async_write_init(void);
void log_async_write(const void * data, int size);
void log_async_write_deinit(void);

// Essential monitoring - only track data loss
uint32_t log_async_get_lost_count(void);

#endif
