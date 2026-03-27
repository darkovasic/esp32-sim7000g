/*
 * UART2 to SIM7000 (BK-7000) — DevKitC: GPIO17 TX -> modem R, GPIO16 RX <- modem T, GND common.
 */
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "modem_uart";

#define MODEM_UART_NUM UART_NUM_2
#define MODEM_TX_PIN   17
#define MODEM_RX_PIN   16
#define MODEM_BAUD     115200

#define BUF_SIZE       512

static void modem_uart_init(void)
{
    const uart_config_t cfg = {
        .baud_rate = MODEM_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(MODEM_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MODEM_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(MODEM_UART_NUM, MODEM_TX_PIN, MODEM_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void drain_uart(void)
{
    uint8_t tmp[256];
    int n;
    while ((n = uart_read_bytes(MODEM_UART_NUM, tmp, sizeof(tmp), 0)) > 0) {
        (void)n;
    }
}

void app_main(void)
{
    modem_uart_init();
    ESP_LOGI(TAG, "UART2 %d baud: TX=%d RX=%d", MODEM_BAUD, MODEM_TX_PIN, MODEM_RX_PIN);

    const char *at = "AT\r\n";
    while (1) {
        drain_uart();
        uart_write_bytes(MODEM_UART_NUM, at, strlen(at));
        ESP_LOGI(TAG, "Sent AT");

        uint8_t buf[BUF_SIZE];
        int total = 0;
        int64_t start = esp_timer_get_time();
        while (total < (int)sizeof(buf) - 1) {
            int n = uart_read_bytes(MODEM_UART_NUM, buf + total,
                                    sizeof(buf) - 1 - total, pdMS_TO_TICKS(500));
            if (n <= 0) {
                if (esp_timer_get_time() - start > 2 * 1000 * 1000) {
                    break;
                }
                continue;
            }
            total += n;
            start = esp_timer_get_time();
            if (memchr(buf, '\n', total)) {
                break;
            }
        }
        buf[total] = '\0';
        if (total > 0) {
            ESP_LOGI(TAG, "RX (%d bytes): %s", total, buf);
        } else {
            ESP_LOGW(TAG, "No response (GND, TX/RX, power, try baud 9600)");
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
