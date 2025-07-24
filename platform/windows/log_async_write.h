#ifndef _LOG_ASYNC_WRITE_H
#define _LOG_ASYNC_WRITE_H
#include <stdint.h>

// Core API
void log_async_write_init(void);
void log_async_write(const void * data, int size);

// Optional: Statistics and control functions
uint32_t log_async_get_lost_count(void);
uint32_t log_async_get_buffer_usage(void);
uint32_t log_async_get_buffer_free(void);
void log_async_reset_stats(void);
void log_async_write_deinit(void);

#endif
