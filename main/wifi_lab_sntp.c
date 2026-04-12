#include "wifi_lab_sntp.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

#if CONFIG_READINGS_UPLOAD_ENABLE && !CONFIG_APP_ENABLE_CELLULAR
#include "readings_config.h"
#include "readings_upload.h"
#endif

static const char *TAG = "wifi_sta";

#define WIFI_GOT_IP_BIT BIT0
#define WIFI_FAIL_BIT   BIT1

static EventGroupHandle_t s_wifi_ev;

static void wifi_sta_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            xEventGroupSetBits(s_wifi_ev, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_ev, WIFI_GOT_IP_BIT);
    }
}

static void wifi_sta_unregister_events(void)
{
    (void)esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event);
    (void)esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_sta_event);
}

static esp_err_t wifi_sta_connect_ip(const char *ssid, const char *password, uint32_t connect_timeout_ms)
{
    if (ssid == NULL || ssid[0] == '\0') {
        ESP_LOGE(TAG, "Wi-Fi SSID is empty");
        return ESP_ERR_INVALID_ARG;
    }

    s_wifi_ev = xEventGroupCreate();
    if (s_wifi_ev == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&icfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        vEventGroupDelete(s_wifi_ev);
        s_wifi_ev = NULL;
        return err;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event, NULL);
    if (err != ESP_OK) {
        goto fail_wifi_init;
    }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_sta_event, NULL);
    if (err != ESP_OK) {
        wifi_sta_unregister_events();
        goto fail_wifi_init;
    }

    wifi_config_t wcfg = {0};
    strncpy((char *)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char *)wcfg.sta.password, password, sizeof(wcfg.sta.password) - 1);
    }
    if (password == NULL || password[0] == '\0') {
        wcfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        goto fail_handlers;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    if (err != ESP_OK) {
        goto fail_handlers;
    }

    xEventGroupClearBits(s_wifi_ev, WIFI_GOT_IP_BIT | WIFI_FAIL_BIT);
    err = esp_wifi_start();
    if (err != ESP_OK) {
        goto fail_handlers;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_ev, WIFI_GOT_IP_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(connect_timeout_ms));
    if ((bits & WIFI_GOT_IP_BIT) == 0) {
        ESP_LOGW(TAG, "Wi-Fi did not get IP (timeout or disconnect)");
        err = ESP_ERR_TIMEOUT;
        goto fail_handlers;
    }

    return ESP_OK;

fail_handlers:
    wifi_sta_unregister_events();
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();

fail_wifi_init:
    vEventGroupDelete(s_wifi_ev);
    s_wifi_ev = NULL;
    return err;
}

static void wifi_sta_teardown(void)
{
    wifi_sta_unregister_events();
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();
    if (s_wifi_ev != NULL) {
        vEventGroupDelete(s_wifi_ev);
        s_wifi_ev = NULL;
    }
}

#if CONFIG_READINGS_UPLOAD_ENABLE && !CONFIG_APP_ENABLE_CELLULAR
static void readings_upload_once(void)
{
    esp_err_t e = readings_upload_run_from_nvs_blocking();
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG,
                 "Readings: missing NVS %s/%s — skip upload",
                 READINGS_NVS_NAMESPACE,
                 READINGS_NVS_KEY_API_KEY);
    } else if (e != ESP_OK) {
        ESP_LOGW(TAG, "Readings upload failed: %s", esp_err_to_name(e));
    }
}
#endif

#if CONFIG_LAB_WIFI_SNTP
esp_err_t wifi_lab_sntp_sync_and_teardown(void)
{
    esp_err_t err = wifi_sta_connect_ip(CONFIG_LAB_WIFI_SSID, CONFIG_LAB_WIFI_PASSWORD,
                                        (uint32_t)CONFIG_LAB_WIFI_CONNECT_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_LAB_WIFI_SNTP_SERVER);
    err = esp_netif_sntp_init(&sntp_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_sntp_init: %s", esp_err_to_name(err));
        wifi_sta_teardown();
        return err;
    }

    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(CONFIG_LAB_SNTP_SYNC_TIMEOUT_MS));
    esp_netif_sntp_deinit();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync: %s", esp_err_to_name(err));
        wifi_sta_teardown();
        return err;
    }

    if (setenv("TZ", CONFIG_LAB_TZ_POSIX, 1) != 0) {
        ESP_LOGW(TAG, "setenv(TZ) failed");
    } else {
        tzset();
    }

    time_t now = time(NULL);
    struct tm tm_utc;
    struct tm tm_local;
    gmtime_r(&now, &tm_utc);
    localtime_r(&now, &tm_local);
    char buf_utc[32];
    char buf_loc[32];
    strftime(buf_utc, sizeof(buf_utc), "%Y-%m-%d %H:%M:%S UTC", &tm_utc);
    strftime(buf_loc, sizeof(buf_loc), "%Y-%m-%d %H:%M:%S %Z", &tm_local);
    ESP_LOGI(TAG, "Time synced: %s | local: %s", buf_utc, buf_loc);

#if CONFIG_READINGS_UPLOAD_ENABLE && !CONFIG_APP_ENABLE_CELLULAR
    readings_upload_once();
#endif

    wifi_sta_teardown();
    return ESP_OK;
}
#endif

#if CONFIG_READINGS_UPLOAD_ENABLE && !CONFIG_APP_ENABLE_CELLULAR && !CONFIG_LAB_WIFI_SNTP
esp_err_t wifi_readings_sta_upload_and_teardown(void)
{
    esp_err_t err = wifi_sta_connect_ip(CONFIG_READINGS_WIFI_SSID, CONFIG_READINGS_WIFI_PASSWORD,
                                        (uint32_t)CONFIG_READINGS_WIFI_CONNECT_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    readings_upload_once();
    wifi_sta_teardown();
    return ESP_OK;
}
#endif
