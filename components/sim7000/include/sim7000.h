#pragma once

#include "esp_err.h"
#include "esp_modem_c_api_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *apn;
} sim7000_config_t;

/**
 * Run identification + SIM + registration (+ optional PDP) via esp_modem.
 * Requires a DCE created with esp_modem_new_dev() (UART owned by esp_modem).
 */
esp_err_t sim7000_bringup(esp_modem_dce_t *dce, const sim7000_config_t *cfg);

#ifdef __cplusplus
}
#endif
