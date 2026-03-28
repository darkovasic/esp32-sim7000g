#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*at_urc_handler_t)(const char *line, void *user_ctx);

/**
 * Start the modem worker task and queues. Requires modem_uart_init() already called.
 */
esp_err_t at_client_init(void);

void at_client_deinit(void);

/**
 * Register a single URC handler (optional). Invoked from the modem task for lines
 * that look like URCs (+PREFIX: or ^PREFIX:) while waiting for command completion.
 */
void at_client_set_urc_handler(at_urc_handler_t handler, void *user_ctx);

/**
 * Synchronous AT command. Appends \\r\\n if missing. Waits for OK, ERROR, or +CME ERROR.
 * Intermediate non-URC lines are concatenated into `response` (NUL-terminated) if non-NULL.
 */
esp_err_t at_client_cmd(const char *cmd, char *response, size_t response_cap, int timeout_ms,
                        int retries);

/** Convenience: default timeout and retries from Kconfig. */
esp_err_t at_client_cmd_simple(const char *cmd, char *response, size_t response_cap);

#ifdef __cplusplus
}
#endif
