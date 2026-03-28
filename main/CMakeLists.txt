idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES sim7000 at_client modem_uart nvs_flash esp_app_format
                             espressif__esp_modem)
