/*
 * ESP32 + SIM7000: layered UART / AT / SIM7000 bring-up.
 * Wiring: see README and `idf.py menuconfig` -> Modem UART transport.
 */
#include <stdio.h>
#include <string.h>

#include "at_client.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modem_config.h"
#include "modem_uart.h"
#include "nvs_flash.h"
#include "sim7000.h"
#include "sdkconfig.h"

static const char *TAG = "app";

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Firmware %s", app->version);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    char apn[64];
    esp_err_t apn_err = modem_config_load_apn(apn, sizeof(apn));
    if (apn_err != ESP_OK) {
        ESP_LOGI(TAG, "APN from Kconfig default (NVS not set or namespace missing)");
    } else {
        ESP_LOGI(TAG, "APN (NVS or default): %s", apn);
    }

    ESP_ERROR_CHECK(modem_uart_init());

#if CONFIG_MODEM_PWRKEY_ENABLE
    (void)modem_pwrkey_pulse_ms(1200);
    vTaskDelay(pdMS_TO_TICKS(3000));
#endif

    ESP_ERROR_CHECK(at_client_init());

    sim7000_config_t mcfg = {.apn = apn};
    err = sim7000_bringup(&mcfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Bring-up incomplete: %s — continuing AT heartbeat", esp_err_to_name(err));
    }

    const char *at = "AT";
    while (1) {
        char resp[CONFIG_AT_CLIENT_MAX_AGG_LEN];
        err = at_client_cmd_simple(at, resp, sizeof(resp));
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "AT OK%s%s", resp[0] ? " " : "", resp[0] ? resp : "");
        } else {
            ESP_LOGW(TAG, "No AT response (%s) — check wiring, power, baud", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
