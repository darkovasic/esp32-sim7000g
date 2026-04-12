#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * HTTPS POST { "device_id", "value" } to api_base + "/data".
 * api_base should be like https://host (trailing slashes stripped).
 * device_id must not contain JSON-special characters (e.g. " or \\).
 */
esp_err_t readings_upload_post(const char *api_key, const char *api_base, const char *device_id,
                               double value);

/** Load NVS/Kconfig readings fields and POST /data on a dedicated task (avoids main-stack TLS overflow). */
esp_err_t readings_upload_run_from_nvs_blocking(void);

#ifdef __cplusplus
}
#endif
