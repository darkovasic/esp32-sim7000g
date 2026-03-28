#include "sim7000.h"

#include <stdio.h>
#include <string.h>

#include "at_client.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "sim7000";

static void urc_log(const char *line, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "URC: %s", line);
}

static esp_err_t cmd_ok(const char *c)
{
    char resp[CONFIG_AT_CLIENT_MAX_AGG_LEN];
    esp_err_t e = at_client_cmd_simple(c, resp, sizeof(resp));
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

esp_err_t sim7000_bringup(const sim7000_config_t *cfg)
{
    at_client_set_urc_handler(urc_log, NULL);

    ESP_RETURN_ON_ERROR(cmd_ok("AT"), TAG, "AT");
    ESP_RETURN_ON_ERROR(cmd_ok("ATE0"), TAG, "ATE0");

    char resp[CONFIG_AT_CLIENT_MAX_AGG_LEN];
    esp_err_t e = at_client_cmd_simple("AT+CGMI", resp, sizeof(resp));
    if (e == ESP_OK && resp[0]) {
        ESP_LOGI(TAG, "Manufacturer: %s", resp);
    }
    e = at_client_cmd_simple("AT+CGMM", resp, sizeof(resp));
    if (e == ESP_OK && resp[0]) {
        ESP_LOGI(TAG, "Model: %s", resp);
    }

    ESP_RETURN_ON_ERROR(cmd_ok("AT+CPIN?"), TAG, "CPIN");

    e = at_client_cmd_simple("AT+CREG?", resp, sizeof(resp));
    if (e != ESP_OK) {
        return e;
    }
    ESP_LOGI(TAG, "CREG: %s", resp[0] ? resp : "(empty)");

    e = at_client_cmd_simple("AT+CGREG?", resp, sizeof(resp));
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
    ESP_RETURN_ON_ERROR(at_client_cmd_simple(set_apn, resp, sizeof(resp)), TAG, "CGDCONT");
    char act[32];
    snprintf(act, sizeof(act), "AT+CGACT=1,%d", CONFIG_SIM7000_PDP_CID);
    ESP_RETURN_ON_ERROR(at_client_cmd_simple(act, resp, sizeof(resp)), TAG, "CGACT");
    ESP_LOGI(TAG, "PDP context %d activated (APN=%s)", CONFIG_SIM7000_PDP_CID, apn);
#endif

    return ESP_OK;
}
