#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configure PWRKEY GPIO from Kconfig (no-op if disabled).
 * UART to the modem is owned by esp_modem; this component is board/power helpers only.
 */
esp_err_t modem_pwrkey_gpio_init(void);

/**
 * Optional power-on pulse via PWRKEY GPIO (no-op if disabled in Kconfig).
 * Adjust timing in implementation if your module datasheet differs.
 */
esp_err_t modem_pwrkey_pulse_ms(uint32_t hold_ms);

#ifdef __cplusplus
}
#endif
