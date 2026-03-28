#include "sim7000.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_modem_api.h"
#include "sdkconfig.h"

static const char *TAG = "sim7000";

static esp_err_t cmd_ok(esp_modem_dce_t *dce, const char *c)
{
    char resp[CONFIG_AT_CLIENT_MAX_AGG_LEN];
    esp_err_t e = esp_modem_at(dce, c, resp, CONFIG_AT_CLIENT_DEFAULT_TIMEOUT_MS);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "FAIL %s -> %s", c, esp_err_to_name(e));
        if (resp[0]) {
            ESP_LOGW(TAG, "resp: %s", resp);
        }
    } else if (resp[0]) {
        ESP_LOGI(TAG, "%s => %s", c, resp);
    }
    return e;
}

esp_err_t sim7000_bringup(esp_modem_dce_t *dce, const sim7000_config_t *cfg)
{
    ESP_RETURN_ON_ERROR(esp_modem_sync(dce), TAG, "esp_modem_sync");
    ESP_RETURN_ON_ERROR(cmd_ok(dce, "ATE0"), TAG, "ATE0");

    char resp[CONFIG_AT_CLIENT_MAX_AGG_LEN];
    esp_err_t e = esp_modem_at(dce, "AT+CGMI", resp, CONFIG_AT_CLIENT_DEFAULT_TIMEOUT_MS);
    if (e == ESP_OK && resp[0]) {
        ESP_LOGI(TAG, "Manufacturer: %s", resp);
    }
    e = esp_modem_at(dce, "AT+CGMM", resp, CONFIG_AT_CLIENT_DEFAULT_TIMEOUT_MS);
    if (e == ESP_OK && resp[0]) {
        ESP_LOGI(TAG, "Model: %s", resp);
    }

    ESP_RETURN_ON_ERROR(cmd_ok(dce, "AT+CPIN?"), TAG, "CPIN");

    e = esp_modem_at(dce, "AT+CREG?", resp, CONFIG_AT_CLIENT_DEFAULT_TIMEOUT_MS);
    if (e != ESP_OK) {
        return e;
    }
    ESP_LOGI(TAG, "CREG: %s", resp[0] ? resp : "(empty)");

    e = esp_modem_at(dce, "AT+CGREG?", resp, CONFIG_AT_CLIENT_DEFAULT_TIMEOUT_MS);
    if (e != ESP_OK) {
        return e;
    }
    ESP_LOGI(TAG, "CGREG: %s", resp[0] ? resp : "(empty)");

#if CONFIG_SIM7000_BRINGUP_PDP
    const char *apn = (cfg && cfg->apn && cfg->apn[0]) ? cfg->apn : CONFIG_SIM7000_DEFAULT_APN;
    char set_apn[160];
    int n = snprintf(set_apn, sizeof(set_apn), "AT+CGDCONT=%d,\"IP\",\"%s\"", CONFIG_SIM7000_PDP_CID,
                     apn);
    if (n <= 0 || n >= (int)sizeof(set_apn)) {
        ESP_LOGE(TAG, "APN too long");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(esp_modem_at(dce, set_apn, resp, CONFIG_AT_CLIENT_DEFAULT_TIMEOUT_MS), TAG,
                        "CGDCONT");
    char act[32];
    snprintf(act, sizeof(act), "AT+CGACT=1,%d", CONFIG_SIM7000_PDP_CID);
    ESP_RETURN_ON_ERROR(esp_modem_at(dce, act, resp, CONFIG_AT_CLIENT_DEFAULT_TIMEOUT_MS), TAG,
                        "CGACT");
    ESP_LOGI(TAG, "PDP context %d activated (APN=%s)", CONFIG_SIM7000_PDP_CID, apn);
#endif

    return ESP_OK;
}
