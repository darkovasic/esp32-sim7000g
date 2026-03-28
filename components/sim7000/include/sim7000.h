#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *apn;
} sim7000_config_t;

/**
 * Run identification + SIM + registration (+ optional PDP). Requires:
 * modem_uart_init(), at_client_init().
 */
esp_err_t sim7000_bringup(const sim7000_config_t *cfg);

#ifdef __cplusplus
}
#endif
