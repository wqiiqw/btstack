#ifndef HCI_DUMP_EPM_EMBEDDED_ASYNC_UART_H
#define HCI_DUMP_EPM_EMBEDDED_ASYNC_UART_H

#include <stdint.h>
#include <stdarg.h>       // for va_list
#include "hci_dump.h"

#if defined __cplusplus
extern "C" {
#endif

/**
 * @brief Get HCI Dump Windows STDOUT Instance
 * @return hci_dump_impl
 */
const hci_dump_t * hci_dump_epm_embedded_async_uart_get_instance(void);

/* API_END */

#if defined __cplusplus
}
#endif
#endif // HCI_DUMP_WINDOWS_STDOUT_H
