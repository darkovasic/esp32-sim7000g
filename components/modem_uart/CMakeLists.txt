idf_component_register(SRCS "modem_uart.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_uart esp_driver_gpio)
