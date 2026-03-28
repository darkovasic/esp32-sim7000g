#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODEM_NVS_NAMESPACE "modem"
#define MODEM_NVS_KEY_APN   "apn"

/**
 * Load APN from NVS into `out` (NUL-terminated). On missing key, copies
 * CONFIG_SIM7000_DEFAULT_APN and returns ESP_ERR_NVS_NOT_FOUND (still usable).
 */
esp_err_t modem_config_load_apn(char *out, size_t out_cap);

/** Store APN for next boot (provisioning / setup). */
esp_err_t modem_config_save_apn(const char *apn);

#ifdef __cplusplus
}
#endif
