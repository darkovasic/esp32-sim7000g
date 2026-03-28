#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Install UART driver and configure pins from Kconfig. Call once before AT traffic. */
esp_err_t modem_uart_init(void);

/** Discard all bytes currently in the RX buffer. */
void modem_uart_drain(void);

/** Write raw bytes to the modem (non-blocking where possible). */
int modem_uart_write(const uint8_t *data, size_t len);

/**
 * Read up to `cap` bytes, blocking up to `ticks_to_wait`.
 * @return bytes read, or negative errno-style code
 */
int modem_uart_read(uint8_t *buf, size_t cap, TickType_t ticks_to_wait);

uart_port_t modem_uart_port(void);

/**
 * Optional power-on pulse via PWRKEY GPIO (no-op if disabled in Kconfig).
 * Adjust timing in implementation if your module datasheet differs.
 */
esp_err_t modem_pwrkey_pulse_ms(uint32_t hold_ms);

#ifdef __cplusplus
}
#endif
