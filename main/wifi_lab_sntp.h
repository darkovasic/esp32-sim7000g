#pragma once

#include "esp_err.h"
#include "sdkconfig.h"

#if CONFIG_LAB_WIFI_SNTP
/**
 * Lab: STA connect, SNTP sync, set TZ, optional readings upload (when cellular off), tear down Wi‑Fi.
 * Call after esp_netif_init() and esp_event_loop_create_default().
 */
esp_err_t wifi_lab_sntp_sync_and_teardown(void);
#endif

#if CONFIG_READINGS_UPLOAD_ENABLE && !CONFIG_APP_ENABLE_CELLULAR && !CONFIG_LAB_WIFI_SNTP
/** When cellular is off and lab Wi‑Fi is not used: STA connect, POST /data once, disconnect. */
esp_err_t wifi_readings_sta_upload_and_teardown(void);
#endif
