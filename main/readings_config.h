#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define READINGS_NVS_NAMESPACE   "readings"
#define READINGS_NVS_KEY_API_KEY "api_key"
#define READINGS_NVS_KEY_API_BASE "api_base"
#define READINGS_NVS_KEY_DEVICE_ID "device_id"

/**
 * Load settings for POST /data. api_key_out must be non-NULL; base_url_out and device_id_out
 * receive Kconfig defaults when NVS keys are missing.
 *
 * Returns ESP_ERR_NVS_NOT_FOUND if api_key is missing (out buffers still filled where applicable).
 */
esp_err_t readings_config_load(char *api_key_out, size_t api_key_cap, char *base_url_out,
                               size_t base_url_cap, char *device_id_out, size_t device_id_cap);

esp_err_t readings_config_save_api_key(const char *api_key);
esp_err_t readings_config_save_api_base(const char *api_base);
esp_err_t readings_config_save_device_id(const char *device_id);

#ifdef __cplusplus
}
#endif
