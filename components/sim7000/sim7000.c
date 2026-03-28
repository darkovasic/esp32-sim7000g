#include "sim7000.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_modem_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "sim7000";

static esp_err_t cmd_ok(esp_modem_dce_t *dce, const char *c)
{
    char resp[CONFIG_SIM7000_AT_RESP_MAX_LEN];
    esp_err_t e = esp_modem_at(dce, c, resp, CONFIG_SIM7000_AT_TIMEOUT_MS);
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

    char resp[CONFIG_SIM7000_AT_RESP_MAX_LEN];
    esp_err_t e = esp_modem_at(dce, "AT+CGMI", resp, CONFIG_SIM7000_AT_TIMEOUT_MS);
    if (e == ESP_OK && resp[0]) {
        ESP_LOGI(TAG, "Manufacturer: %s", resp);
    }
    e = esp_modem_at(dce, "AT+CGMM", resp, CONFIG_SIM7000_AT_TIMEOUT_MS);
    if (e == ESP_OK && resp[0]) {
        ESP_LOGI(TAG, "Model: %s", resp);
    }

    ESP_RETURN_ON_ERROR(cmd_ok(dce, "AT+CPIN?"), TAG, "CPIN");

    e = esp_modem_at(dce, "AT+CREG?", resp, CONFIG_SIM7000_AT_TIMEOUT_MS);
    if (e != ESP_OK) {
        return e;
    }
    ESP_LOGI(TAG, "CREG: %s", resp[0] ? resp : "(empty)");

    e = esp_modem_at(dce, "AT+CGREG?", resp, CONFIG_SIM7000_AT_TIMEOUT_MS);
    if (e != ESP_OK) {
        return e;
    }
    ESP_LOGI(TAG, "CGREG: %s", resp[0] ? resp : "(empty)");

    e = esp_modem_at(dce, "AT+CEREG?", resp, CONFIG_SIM7000_AT_TIMEOUT_MS);
    if (e == ESP_OK && resp[0]) {
        ESP_LOGI(TAG, "CEREG (LTE/EPS): %s", resp);
    } else if (e != ESP_OK) {
        ESP_LOGW(TAG, "AT+CEREG? failed (%s) — LTE registration unknown", esp_err_to_name(e));
    }

#if CONFIG_SIM7000_BRINGUP_PDP
    const char *apn = (cfg && cfg->apn && cfg->apn[0]) ? cfg->apn : CONFIG_SIM7000_DEFAULT_APN;
    char set_apn[160];
    int n = snprintf(set_apn, sizeof(set_apn), "AT+CGDCONT=%d,\"IP\",\"%s\"", CONFIG_SIM7000_PDP_CID,
                     apn);
    if (n <= 0 || n >= (int)sizeof(set_apn)) {
        ESP_LOGE(TAG, "APN too long");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(esp_modem_at(dce, set_apn, resp, CONFIG_SIM7000_AT_TIMEOUT_MS), TAG,
                        "CGDCONT");
#if CONFIG_SIM7000_BRINGUP_USE_CGACT
    char act[32];
    snprintf(act, sizeof(act), "AT+CGACT=1,%d", CONFIG_SIM7000_PDP_CID);
    ESP_RETURN_ON_ERROR(esp_modem_at(dce, act, resp, CONFIG_SIM7000_AT_TIMEOUT_MS), TAG,
                        "CGACT");
    ESP_LOGI(TAG, "PDP CID %d: CGDCONT + CGACT (APN=%s)", CONFIG_SIM7000_PDP_CID, apn);
#else
    ESP_LOGI(TAG, "PDP CID %d: CGDCONT only (APN=%s); PPP dial will activate context", CONFIG_SIM7000_PDP_CID,
             apn);
#endif
#endif

    return ESP_OK;
}

/** Parse +CREG/+CGREG/+CEREG n,stat from aggregated AT response; returns stat or -1 if missing. */
static int sim7000_reg_stat(const char *resp, const char *prefix)
{
    const char *p = strstr(resp, prefix);
    if (p == NULL) {
        return -1;
    }
    p += strlen(prefix);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    int n = 0;
    int stat = -1;
    if (sscanf(p, "%d,%d", &n, &stat) >= 2) {
        return stat;
    }
    return -1;
}

static bool sim7000_is_registered_stat(int stat)
{
    return stat == 1 || stat == 5;
}

esp_err_t sim7000_wait_for_network_registration(esp_modem_dce_t *dce)
{
#if !CONFIG_SIM7000_WAIT_FOR_REGISTRATION
    (void)dce;
    return ESP_OK;
#else
    char resp[CONFIG_SIM7000_AT_RESP_MAX_LEN];
    const uint32_t poll_ms = (uint32_t)CONFIG_SIM7000_REGISTRATION_POLL_MS;
    const uint32_t timeout_ms = (uint32_t)CONFIG_SIM7000_REGISTRATION_TIMEOUT_MS;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    ESP_LOGI(TAG, "Waiting for network registration (poll %lu ms, timeout %lu ms)",
             (unsigned long)poll_ms, (unsigned long)timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        int creg = -1;
        int cgreg = -1;
        int cereg = -1;

        if (esp_modem_at(dce, "AT+CREG?", resp, CONFIG_SIM7000_AT_TIMEOUT_MS) == ESP_OK) {
            creg = sim7000_reg_stat(resp, "+CREG:");
        }
        if (esp_modem_at(dce, "AT+CGREG?", resp, CONFIG_SIM7000_AT_TIMEOUT_MS) == ESP_OK) {
            cgreg = sim7000_reg_stat(resp, "+CGREG:");
        }
        if (esp_modem_at(dce, "AT+CEREG?", resp, CONFIG_SIM7000_AT_TIMEOUT_MS) == ESP_OK) {
            cereg = sim7000_reg_stat(resp, "+CEREG:");
        }

        if (sim7000_is_registered_stat(creg) || sim7000_is_registered_stat(cgreg) ||
            sim7000_is_registered_stat(cereg)) {
            ESP_LOGI(TAG, "Registered: CREG=%d CGREG=%d CEREG=%d", creg, cgreg, cereg);
            return ESP_OK;
        }

        ESP_LOGI(TAG, "Still searching (CREG=%d CGREG=%d CEREG=%d)", creg, cgreg, cereg);
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
    }

    return ESP_ERR_TIMEOUT;
#endif
}
