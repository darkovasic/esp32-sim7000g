#include "readings_config.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "readings_cfg";

static void copy_default_or_nvs_str(nvs_handle_t h, const char *key, char *out, size_t cap,
                                    const char *kconfig_default)
{
    if (out == NULL || cap == 0) {
        return;
    }
    size_t len = cap;
    esp_err_t err = (h != 0) ? nvs_get_str(h, key, out, &len) : ESP_ERR_NVS_NOT_FOUND;
    if (err != ESP_OK) {
        strncpy(out, kconfig_default, cap - 1);
        out[cap - 1] = '\0';
    }
}

esp_err_t readings_config_load(char *api_key_out, size_t api_key_cap, char *base_url_out,
                               size_t base_url_cap, char *device_id_out, size_t device_id_cap)
{
    if (api_key_out == NULL || api_key_cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }

#if !CONFIG_READINGS_UPLOAD_ENABLE
    api_key_out[0] = '\0';
    if (base_url_out && base_url_cap) {
        base_url_out[0] = '\0';
    }
    if (device_id_out && device_id_cap) {
        device_id_out[0] = '\0';
    }
    return ESP_ERR_NOT_SUPPORTED;
#else

    nvs_handle_t h = 0;
    esp_err_t open_err = nvs_open(READINGS_NVS_NAMESPACE, NVS_READONLY, &h);
    if (open_err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace '%s' open: %s — using Kconfig defaults (no api_key)",
                 READINGS_NVS_NAMESPACE, esp_err_to_name(open_err));
        api_key_out[0] = '\0';
        copy_default_or_nvs_str(0, NULL, base_url_out, base_url_cap, CONFIG_READINGS_API_BASE_URL);
        copy_default_or_nvs_str(0, NULL, device_id_out, device_id_cap, CONFIG_READINGS_DEVICE_ID);
        return ESP_ERR_NVS_NOT_FOUND;
    }

    size_t len = api_key_cap;
    esp_err_t err = nvs_get_str(h, READINGS_NVS_KEY_API_KEY, api_key_out, &len);
    if (err != ESP_OK) {
        api_key_out[0] = '\0';
    }

    copy_default_or_nvs_str(h, READINGS_NVS_KEY_API_BASE, base_url_out, base_url_cap,
                            CONFIG_READINGS_API_BASE_URL);
    copy_default_or_nvs_str(h, READINGS_NVS_KEY_DEVICE_ID, device_id_out, device_id_cap,
                            CONFIG_READINGS_DEVICE_ID);

    nvs_close(h);

    if (err != ESP_OK) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (api_key_out[0] == '\0') {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    return ESP_OK;
#endif
}

esp_err_t readings_config_save_api_key(const char *api_key)
{
    if (api_key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(READINGS_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(h, READINGS_NVS_KEY_API_KEY, api_key);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t readings_config_save_api_base(const char *api_base)
{
    if (api_base == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(READINGS_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, READINGS_NVS_KEY_API_BASE, api_base);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t readings_config_save_device_id(const char *device_id)
{
    if (device_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(READINGS_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, READINGS_NVS_KEY_DEVICE_ID, device_id);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
