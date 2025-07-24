#ifndef _LOG_ASYNC_WRITE_H
#define _LOG_ASYNC_WRITE_H
#include <stdint.h>

// Core API
void log_async_write_init(void);
void log_async_write(const void * data, int size);
void log_async_write_deinit(void);

// Essential monitoring - only track data loss
uint32_t log_async_get_lost_count(void);

#endif
