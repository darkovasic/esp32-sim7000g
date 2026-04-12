#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * HTTPS POST to api_base + "/data".
 * api_base should be like https://host (trailing slashes stripped).
 * device_id must not contain JSON-special characters (e.g. " or \\).
 * value is a JSON number.
 * If metric_opt is NULL or empty, the metric field is omitted (server defaults to uptime_ms).
 */
esp_err_t readings_upload_post(const char *api_key, const char *api_base, const char *device_id,
                               double value_ms, const char *metric_opt);

/** Load NVS/Kconfig readings fields and POST /data on a dedicated task (avoids main-stack TLS overflow). */
esp_err_t readings_upload_run_from_nvs_blocking(void);

/**
 * Same as readings_upload_run_from_nvs_blocking, but if post_wifi_rssi is true also POST STA RSSI
 * (metric wifi_rssi_dbm) after uptime — Wi-Fi STA must still be up when the worker runs.
 */
esp_err_t readings_upload_run_from_nvs_blocking_ex(bool post_wifi_rssi);

#ifdef __cplusplus
}
#endif
