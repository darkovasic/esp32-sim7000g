#include "readings_upload.h"

#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "readings_up";

static void trim_trailing_slashes(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == '/') {
        s[n - 1] = '\0';
        n--;
    }
}

esp_err_t readings_upload_post(const char *api_key, const char *api_base, const char *device_id,
                               double value)
{
#if !CONFIG_READINGS_UPLOAD_ENABLE
    (void)api_key;
    (void)api_base;
    (void)device_id;
    (void)value;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (api_key == NULL || api_key[0] == '\0' || api_base == NULL || api_base[0] == '\0' ||
        device_id == NULL || device_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char base[160];
    strncpy(base, api_base, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    trim_trailing_slashes(base);

    char url[sizeof(base) + 16];
    int un = snprintf(url, sizeof(url), "%s/data", base);
    if (un <= 0 || un >= (int)sizeof(url)) {
        ESP_LOGE(TAG, "API URL too long");
        return ESP_ERR_INVALID_ARG;
    }

    char auth[288];
    int an = snprintf(auth, sizeof(auth), "Bearer %s", api_key);
    if (an <= 0 || an >= (int)sizeof(auth)) {
        ESP_LOGE(TAG, "Authorization header too long (shorten API key)");
        return ESP_ERR_INVALID_ARG;
    }

    char body[512];
    int bn = snprintf(body, sizeof(body), "{\"device_id\":\"%s\",\"value\":%.17g}", device_id, value);
    if (bn <= 0 || bn >= (int)sizeof(body)) {
        ESP_LOGE(TAG, "JSON body too long (shorten device_id)");
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CONFIG_READINGS_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth);

    esp_err_t err = esp_http_client_set_post_field(client, body, (int)strlen(body));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_post_field: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP perform: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 201) {
        ESP_LOGW(TAG, "HTTP status %d (expected 201)", status);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "POST /data succeeded (201)");
    return ESP_OK;
#endif
}
