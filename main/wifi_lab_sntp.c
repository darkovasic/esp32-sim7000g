#include "wifi_lab_sntp.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

static const char *TAG = "wifi_lab_sntp";

#define WIFI_GOT_IP_BIT BIT0
#define WIFI_FAIL_BIT   BIT1

static EventGroupHandle_t s_wifi_ev;

static void wifi_lab_event(void *arg, esp_event_base_t base, int32_t id, void *data)
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

static void wifi_lab_unregister_events(void)
{
    (void)esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_lab_event);
    (void)esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_lab_event);
}

esp_err_t wifi_lab_sntp_sync_and_teardown(void)
{
    esp_err_t err = ESP_OK;

    if (CONFIG_LAB_WIFI_SSID[0] == '\0') {
        ESP_LOGE(TAG, "LAB_WIFI_SSID is empty");
        return ESP_ERR_INVALID_ARG;
    }

    s_wifi_ev = xEventGroupCreate();
    if (s_wifi_ev == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&icfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        vEventGroupDelete(s_wifi_ev);
        s_wifi_ev = NULL;
        return err;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_lab_event, NULL);
    if (err != ESP_OK) {
        goto fail_wifi_init;
    }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_lab_event, NULL);
    if (err != ESP_OK) {
        wifi_lab_unregister_events();
        goto fail_wifi_init;
    }

    wifi_config_t wcfg = {0};
    strncpy((char *)wcfg.sta.ssid, CONFIG_LAB_WIFI_SSID, sizeof(wcfg.sta.ssid) - 1);
    strncpy((char *)wcfg.sta.password, CONFIG_LAB_WIFI_PASSWORD, sizeof(wcfg.sta.password) - 1);
    if (CONFIG_LAB_WIFI_PASSWORD[0] == '\0') {
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
                                           pdMS_TO_TICKS(CONFIG_LAB_WIFI_CONNECT_TIMEOUT_MS));
    if ((bits & WIFI_GOT_IP_BIT) == 0) {
        ESP_LOGW(TAG, "Wi-Fi did not get IP (timeout or disconnect)");
        err = ESP_ERR_TIMEOUT;
        goto fail_handlers;
    }

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_LAB_WIFI_SNTP_SERVER);
    err = esp_netif_sntp_init(&sntp_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_sntp_init: %s", esp_err_to_name(err));
        goto fail_handlers;
    }

    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(CONFIG_LAB_SNTP_SYNC_TIMEOUT_MS));
    esp_netif_sntp_deinit();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync: %s", esp_err_to_name(err));
        goto fail_handlers;
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
    err = ESP_OK;

fail_handlers:
    wifi_lab_unregister_events();
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();

fail_wifi_init:
    vEventGroupDelete(s_wifi_ev);
    s_wifi_ev = NULL;
    return err;
}
