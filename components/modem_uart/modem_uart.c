#include "modem_uart.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "modem_uart";

static uart_port_t s_port = UART_NUM_MAX;
static bool s_inited;
#if CONFIG_MODEM_PWRKEY_ENABLE
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

uart_port_t modem_uart_port(void)
{
    return s_port;
}

esp_err_t modem_uart_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

#if CONFIG_MODEM_UART_TX_GPIO < 0 || CONFIG_MODEM_UART_RX_GPIO < 0
    ESP_LOGE(TAG, "Invalid UART GPIO in sdkconfig (TX/RX must be >= 0)");
    return ESP_ERR_INVALID_ARG;
#endif

    s_port = (uart_port_t)CONFIG_MODEM_UART_PORT_NUM;

    uart_config_t cfg = {
        .baud_rate = CONFIG_MODEM_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(s_port, CONFIG_MODEM_UART_RX_BUF_SIZE,
                                            CONFIG_MODEM_UART_TX_BUF_SIZE, 0, NULL, 0),
                        TAG, "uart_driver_install");
    ESP_RETURN_ON_ERROR(uart_param_config(s_port, &cfg), TAG, "uart_param_config");
    ESP_RETURN_ON_ERROR(uart_set_pin(s_port, CONFIG_MODEM_UART_TX_GPIO, CONFIG_MODEM_UART_RX_GPIO,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "uart_set_pin");

    ESP_RETURN_ON_ERROR(modem_pwrkey_gpio_init(), TAG, "modem_pwrkey_gpio_init");

    s_inited = true;
    ESP_LOGI(TAG, "UART%d %d baud TX=%d RX=%d", (int)s_port, CONFIG_MODEM_UART_BAUD_RATE,
             CONFIG_MODEM_UART_TX_GPIO, CONFIG_MODEM_UART_RX_GPIO);
    return ESP_OK;
}

void modem_uart_drain(void)
{
    if (!s_inited) {
        return;
    }
    uint8_t tmp[128];
    int n;
    while ((n = uart_read_bytes(s_port, tmp, sizeof(tmp), 0)) > 0) {
        (void)n;
    }
}

int modem_uart_write(const uint8_t *data, size_t len)
{
    if (!s_inited || data == NULL || len == 0) {
        return -1;
    }
    int w = uart_write_bytes(s_port, data, len);
    uart_wait_tx_done(s_port, pdMS_TO_TICKS(500));
    return w;
}

int modem_uart_read(uint8_t *buf, size_t cap, TickType_t ticks_to_wait)
{
    if (!s_inited || buf == NULL || cap == 0) {
        return -1;
    }
    return uart_read_bytes(s_port, buf, cap, ticks_to_wait);
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
