/*
 * ESP32 + SIM7000: esp_modem UART DTE, PPP esp_netif, SIM7000 DCE bring-up.
 * Wiring: see README and `idf.py menuconfig` -> Modem UART transport.
 */
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modem_config.h"
#include "modem_uart.h"
#include "nvs_flash.h"
#include "sim7000.h"
#include "sdkconfig.h"

#if CONFIG_LWIP_PPP_SUPPORT
#include "esp_modem_api.h"
#include "esp_modem_config.h"
#include "esp_modem_dce_config.h"
#endif

static const char *TAG = "app";

#if CONFIG_LWIP_PPP_SUPPORT
static esp_netif_t *s_ppp_netif;
static esp_modem_dce_t *s_dce;
#endif

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

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if !CONFIG_LWIP_PPP_SUPPORT
    ESP_LOGE(TAG, "CONFIG_LWIP_PPP_SUPPORT is required (enable in menuconfig or sdkconfig.defaults)");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
#else

    esp_netif_config_t ppp_cfg = ESP_NETIF_DEFAULT_PPP();
    s_ppp_netif = esp_netif_new(&ppp_cfg);
    if (s_ppp_netif == NULL) {
        ESP_LOGE(TAG, "esp_netif_new(PPP) failed");
        abort();
    }
    ESP_LOGI(TAG, "PPP esp_netif created (%s)", esp_netif_get_desc(s_ppp_netif));

    char apn[64];
    esp_err_t apn_err = modem_config_load_apn(apn, sizeof(apn));
    if (apn_err != ESP_OK) {
        ESP_LOGI(TAG, "APN from Kconfig default (NVS not set or namespace missing)");
    } else {
        ESP_LOGI(TAG, "APN (NVS or default): %s", apn);
    }

    ESP_ERROR_CHECK(modem_pwrkey_gpio_init());

#if CONFIG_MODEM_PWRKEY_ENABLE
    (void)modem_pwrkey_pulse_ms(1200);
    vTaskDelay(pdMS_TO_TICKS(3000));
#endif

#if CONFIG_MODEM_UART_TX_GPIO < 0 || CONFIG_MODEM_UART_RX_GPIO < 0
    ESP_LOGE(TAG, "Invalid UART GPIO in sdkconfig (TX/RX must be >= 0)");
    abort();
#endif

    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.port_num = (uart_port_t)CONFIG_MODEM_UART_PORT_NUM;
    dte_config.uart_config.tx_io_num = CONFIG_MODEM_UART_TX_GPIO;
    dte_config.uart_config.rx_io_num = CONFIG_MODEM_UART_RX_GPIO;
    dte_config.uart_config.rts_io_num = UART_PIN_NO_CHANGE;
    dte_config.uart_config.cts_io_num = UART_PIN_NO_CHANGE;
    dte_config.uart_config.baud_rate = CONFIG_MODEM_UART_BAUD_RATE;
    dte_config.uart_config.flow_control = ESP_MODEM_FLOW_CONTROL_NONE;
    dte_config.uart_config.rx_buffer_size = CONFIG_MODEM_UART_RX_BUF_SIZE;
    dte_config.uart_config.tx_buffer_size =
        (CONFIG_MODEM_UART_TX_BUF_SIZE > 0) ? CONFIG_MODEM_UART_TX_BUF_SIZE : 512;
    dte_config.uart_config.event_queue_size = 30;
    dte_config.dte_buffer_size = CONFIG_MODEM_UART_RX_BUF_SIZE / 2;
    if (dte_config.dte_buffer_size < 256) {
        dte_config.dte_buffer_size = 256;
    }

    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(apn);
    s_dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7000, &dte_config, &dce_config, s_ppp_netif);
    if (s_dce == NULL) {
        ESP_LOGE(TAG, "esp_modem_new_dev(SIM7000) failed");
        abort();
    }
    ESP_LOGI(TAG, "esp_modem DCE (SIM7000) attached to PPP netif");

    sim7000_config_t mcfg = {.apn = apn};
    err = sim7000_bringup(s_dce, &mcfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Bring-up incomplete: %s — continuing esp_modem_sync heartbeat", esp_err_to_name(err));
    }

    while (1) {
        err = esp_modem_sync(s_dce);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "modem sync OK");
        } else {
            ESP_LOGW(TAG, "modem sync failed (%s) — check wiring, power, baud", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
#endif /* CONFIG_LWIP_PPP_SUPPORT */
}
