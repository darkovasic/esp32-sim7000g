#include "modem_uart.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_MODEM_PWRKEY_ENABLE
static const char *TAG = "modem_uart";
static bool s_pwrkey_gpio_inited;
#endif

esp_err_t modem_pwrkey_gpio_init(void)
{
#if !CONFIG_MODEM_PWRKEY_ENABLE
    return ESP_OK;
#else
    if (s_pwrkey_gpio_inited) {
        return ESP_OK;
    }
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_MODEM_PWRKEY_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "pwrkey gpio_config");
    int idle = CONFIG_MODEM_PWRKEY_ACTIVE_LOW ? 1 : 0;
    gpio_set_level(CONFIG_MODEM_PWRKEY_GPIO, idle);
    s_pwrkey_gpio_inited = true;
    return ESP_OK;
#endif
}

esp_err_t modem_pwrkey_pulse_ms(uint32_t hold_ms)
{
#if !CONFIG_MODEM_PWRKEY_ENABLE
    (void)hold_ms;
    return ESP_ERR_NOT_SUPPORTED;
#else
    int active = CONFIG_MODEM_PWRKEY_ACTIVE_LOW ? 0 : 1;
    int idle = CONFIG_MODEM_PWRKEY_ACTIVE_LOW ? 1 : 0;
    gpio_set_level(CONFIG_MODEM_PWRKEY_GPIO, active);
    vTaskDelay(pdMS_TO_TICKS(hold_ms));
    gpio_set_level(CONFIG_MODEM_PWRKEY_GPIO, idle);
    ESP_LOGI(TAG, "PWRKEY pulse %lu ms", (unsigned long)hold_ms);
    return ESP_OK;
#endif
}
