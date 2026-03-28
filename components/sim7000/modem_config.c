#include "modem_config.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "modem_cfg";

esp_err_t modem_config_load_apn(char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(MODEM_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        strncpy(out, CONFIG_SIM7000_DEFAULT_APN, out_cap - 1);
        out[out_cap - 1] = '\0';
        return ESP_ERR_NVS_NOT_FOUND;
    }

    size_t len = out_cap;
    err = nvs_get_str(h, MODEM_NVS_KEY_APN, out, &len);
    nvs_close(h);

    if (err != ESP_OK) {
        strncpy(out, CONFIG_SIM7000_DEFAULT_APN, out_cap - 1);
        out[out_cap - 1] = '\0';
        return ESP_ERR_NVS_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t modem_config_save_apn(const char *apn)
{
    if (apn == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(MODEM_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(h, MODEM_NVS_KEY_APN, apn);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
