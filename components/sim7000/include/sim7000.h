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

/**
 * Poll network registration until CREG, CGREG, or CEREG reports stat 1 (home) or 5 (roaming),
 * or until timeout. Intended after sim7000_bringup() and before esp_modem DATA/PPP when the
 * modem may still be searching (cold boot with SIM).
 */
esp_err_t sim7000_wait_for_network_registration(esp_modem_dce_t *dce);

/**
 * Log signal once in command mode: always AT+CSQ; then either AT+CESQ? (TS 27.007) or, if unsupported,
 * AT+CPSI? (SIM7000) — not both CESQ and CPSI.
 */
void sim7000_log_signal_quality_once(esp_modem_dce_t *dce);

#ifdef __cplusplus
}
#endif
