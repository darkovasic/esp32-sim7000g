#pragma once

#include "esp_err.h"

/**
 * Lab only (CONFIG_LAB_WIFI_SNTP): STA connect, SNTP sync, set TZ, tear down Wi-Fi.
 * Call after esp_netif_init() and esp_event_loop_create_default(), before modem PPP netif.
 */
esp_err_t wifi_lab_sntp_sync_and_teardown(void);
