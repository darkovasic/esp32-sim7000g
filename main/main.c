/*
 * ESP32 + SIM7000: optional esp_modem UART DTE, PPP esp_netif, SIM7000 DCE bring-up + data mode.
 * Wiring: see README and `idf.py menuconfig` -> Modem UART transport / Application.
 */
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#if CONFIG_READINGS_UPLOAD_ENABLE
#include "readings_config.h"
#include "readings_upload.h"
#endif
#if CONFIG_APP_ENABLE_CELLULAR
#include "driver/uart.h"
#include "modem_config.h"
#include "modem_uart.h"
#include "sim7000.h"
#endif
#if CONFIG_LAB_WIFI_SNTP || (CONFIG_READINGS_UPLOAD_ENABLE && !CONFIG_APP_ENABLE_CELLULAR)
#include "wifi_lab_sntp.h"
#endif

#if CONFIG_APP_ENABLE_CELLULAR && CONFIG_LWIP_PPP_SUPPORT
#include "esp_modem_api.h"
#include "esp_modem_config.h"
#include "esp_modem_dce_config.h"
#include "esp_netif_ppp.h"
#include "lwip/ip4_addr.h"
#endif

static const char *TAG = "app";

#if CONFIG_APP_ENABLE_CELLULAR && CONFIG_LWIP_PPP_SUPPORT

/** esp_modem runs setup_data_mode() before dial: ATE0 + CGDCONT for CID 1 with the DCE APN. */
#define PPP_WAIT_IP_TIMEOUT_MS 120000

static esp_netif_t *s_ppp_netif;
static esp_modem_dce_t *s_dce;
static EventGroupHandle_t s_conn_events;

#define CONN_PPP_GOT_IP BIT0
#define CONN_PPP_LOST_IP BIT1

static void on_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == IP_EVENT_PPP_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        if (event->esp_netif != s_ppp_netif) {
            return;
        }
        ESP_LOGI(TAG, "PPP got IP: " IPSTR " mask " IPSTR " gw " IPSTR, IP2STR(&event->ip_info.ip),
                 IP2STR(&event->ip_info.netmask), IP2STR(&event->ip_info.gw));
        esp_netif_dns_info_t dns;
        if (esp_netif_get_dns_info(event->esp_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
            ESP_LOGI(TAG, "DNS: " IPSTR, IP2STR(&dns.ip.u_addr.ip4));
        }
        if (s_conn_events) {
            xEventGroupSetBits(s_conn_events, CONN_PPP_GOT_IP);
        }
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGW(TAG, "PPP lost IP");
        if (s_conn_events) {
            xEventGroupSetBits(s_conn_events, CONN_PPP_LOST_IP);
        }
    }
}

static void on_ppp_phase(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;
    ESP_LOGD(TAG, "NETIF_PPP event %ld", (long)event_id);
}

#endif /* CONFIG_APP_ENABLE_CELLULAR && CONFIG_LWIP_PPP_SUPPORT */

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Firmware %s", app->version);

#if !CONFIG_APP_ENABLE_CELLULAR && CONFIG_APP_DEEP_SLEEP_WHEN_DONE && CONFIG_APP_DEEP_SLEEP_TIMER_SEC > 0
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "Woke from deep sleep (RTC timer)");
    }
#endif

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

#if CONFIG_READINGS_UPLOAD_ENABLE && CONFIG_READINGS_PROVISION_API_KEY_ON_BOOT
    if (CONFIG_READINGS_PROVISION_API_KEY_VALUE[0] != '\0') {
        err = readings_config_save_api_key(CONFIG_READINGS_PROVISION_API_KEY_VALUE);
        if (err == ESP_OK) {
            ESP_LOGI(TAG,
                     "NVS: stored readings/api_key from Kconfig — disable "
                     "READINGS_PROVISION_API_KEY_ON_BOOT and clear the key string, then rebuild");
        } else {
            ESP_LOGE(TAG, "NVS: failed to store readings/api_key: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "READINGS_PROVISION_API_KEY_ON_BOOT is set but READINGS_PROVISION_API_KEY_VALUE "
                      "is empty");
    }
#endif

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if CONFIG_LAB_WIFI_SNTP
    {
        esp_err_t lab = wifi_lab_sntp_sync_and_teardown();
        if (lab != ESP_OK) {
            ESP_LOGW(TAG, "Lab Wi-Fi SNTP: %s — continuing without wall-clock sync", esp_err_to_name(lab));
        }
    }
#elif CONFIG_READINGS_UPLOAD_ENABLE && !CONFIG_APP_ENABLE_CELLULAR
    {
        esp_err_t wr = wifi_readings_sta_upload_and_teardown();
        if (wr != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi readings: %s", esp_err_to_name(wr));
        }
    }
#endif

#if CONFIG_APP_ENABLE_CELLULAR

#if !CONFIG_LWIP_PPP_SUPPORT
    ESP_LOGE(TAG, "APP_ENABLE_CELLULAR requires CONFIG_LWIP_PPP_SUPPORT (menuconfig: LWIP -> PPP)");
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

    /*
     * ESP32 reset often powers only the MCU: the modem can stay up still in PPP from the last run.
     * New DCE state is UNDEF while the UART is PPP-framed → esp_modem_sync times out. From UNDEF,
     * COMMAND runs esp_modem's exit_data path (stop host PPP, optional +++, modem escape to AT).
     */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_err_t cm = esp_modem_set_mode(s_dce, ESP_MODEM_MODE_COMMAND);
    if (cm != ESP_OK) {
        ESP_LOGW(TAG, "esp_modem_set_mode(COMMAND) after boot returned %s — trying bring-up anyway",
                 esp_err_to_name(cm));
    } else {
        ESP_LOGI(TAG, "Modem UART in command mode (recovered from stale PPP if needed)");
    }

    sim7000_config_t mcfg = {.apn = apn};
    err = sim7000_bringup(s_dce, &mcfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Bring-up incomplete: %s — PPP may still fail", esp_err_to_name(err));
    }

#if CONFIG_SIM7000_WAIT_FOR_REGISTRATION
    err = sim7000_wait_for_network_registration(s_dce);
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "Registration wait timed out — attempting PPP anyway");
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Registration wait: %s — attempting PPP anyway", esp_err_to_name(err));
    }
#endif

    sim7000_log_signal_quality_once(s_dce);

    s_conn_events = xEventGroupCreate();
    if (s_conn_events == NULL) {
        ESP_LOGE(TAG, "EventGroupCreate failed");
        abort();
    }
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, on_ppp_phase, NULL));

    xEventGroupClearBits(s_conn_events, CONN_PPP_GOT_IP | CONN_PPP_LOST_IP);

    ESP_LOGI(TAG, "Switching to PPP data mode (stock esp_modem: ATD*99# after CGDCONT)…");
    ESP_LOGI(TAG, "PPP: LCP/IPCP can take tens of seconds; UART is quiet until IP or failure.");
    err = esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_set_mode(DATA) failed: %s", esp_err_to_name(err));
        goto command_heartbeat;
    }

    EventBits_t bits = xEventGroupWaitBits(s_conn_events, CONN_PPP_GOT_IP | CONN_PPP_LOST_IP, pdFALSE,
                                           pdFALSE, pdMS_TO_TICKS(PPP_WAIT_IP_TIMEOUT_MS));
    if (bits & CONN_PPP_LOST_IP) {
        ESP_LOGE(TAG, "PPP lost IP before address assignment");
        (void)esp_modem_set_mode(s_dce, ESP_MODEM_MODE_COMMAND);
        goto command_heartbeat;
    }
    if ((bits & CONN_PPP_GOT_IP) == 0) {
        ESP_LOGE(TAG, "Timeout waiting for PPP IP (%d ms)", PPP_WAIT_IP_TIMEOUT_MS);
        ESP_LOGW(TAG, "Check: carrier APN, SIM7000 RAT/bands, menuconfig LWIP PPP IPv6 off; "
                      "enable CONFIG_LWIP_PPP_DEBUG_ON for lwIP PPP trace; "
                      "for dial-string changes vendor esp_modem (do not patch managed_components).");
        (void)esp_modem_set_mode(s_dce, ESP_MODEM_MODE_COMMAND);
        goto command_heartbeat;
    }

#if CONFIG_READINGS_UPLOAD_ENABLE
    {
        esp_err_t up = readings_upload_run_from_nvs_blocking();
        if (up == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG,
                     "Readings API: missing NVS %s/%s — skip upload. "
                     "Provision once with readings_config_save_api_key() or nvs_set_str.",
                     READINGS_NVS_NAMESPACE,
                     READINGS_NVS_KEY_API_KEY);
        } else if (up != ESP_OK) {
            ESP_LOGW(TAG, "Readings upload failed: %s", esp_err_to_name(up));
        }
    }
#endif

    ESP_LOGI(TAG, "PPP session up; UART is PPP-framed (no AT sync here). Waiting on IP_EVENT / link loss…");

    while (1) {
        EventBits_t lost = xEventGroupWaitBits(s_conn_events, CONN_PPP_LOST_IP, pdTRUE, pdFALSE,
                                               pdMS_TO_TICKS(60000));
        if (lost & CONN_PPP_LOST_IP) {
            ESP_LOGW(TAG, "PPP disconnected");
            (void)esp_modem_set_mode(s_dce, ESP_MODEM_MODE_COMMAND);
            goto command_heartbeat;
        }
        ESP_LOGI(TAG, "PPP still up (idle log every 60 s)");
    }

command_heartbeat:
    ESP_LOGI(TAG, "Command-mode heartbeat (esp_modem_sync)");
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

#else /* !CONFIG_APP_ENABLE_CELLULAR */

    ESP_LOGI(TAG, "Cellular disabled: no modem / PPP");
#if CONFIG_APP_DEEP_SLEEP_WHEN_DONE
#if CONFIG_APP_DEEP_SLEEP_TIMER_SEC > 0
    {
        uint64_t us = (uint64_t)CONFIG_APP_DEEP_SLEEP_TIMER_SEC * 1000000ULL;
        esp_err_t te = esp_sleep_enable_timer_wakeup(us);
        if (te != ESP_OK) {
            ESP_LOGE(TAG, "esp_sleep_enable_timer_wakeup failed: %s", esp_err_to_name(te));
        } else {
            ESP_LOGI(TAG, "Deep sleep; wake in %u s (RTC timer)", (unsigned)CONFIG_APP_DEEP_SLEEP_TIMER_SEC);
        }
    }
#else
    ESP_LOGI(TAG, "Deep sleep until EN/reset (timer wake disabled)");
#endif
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
#else
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        ESP_LOGI(TAG, "idle");
    }
#endif

#endif /* CONFIG_APP_ENABLE_CELLULAR */
}
