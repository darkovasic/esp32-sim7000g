idf_component_register(SRCS "sim7000.c" "modem_config.c"
                    INCLUDE_DIRS "include"
                    REQUIRES espressif__esp_modem freertos nvs_flash)
