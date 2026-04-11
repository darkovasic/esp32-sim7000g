idf_component_register(SRCS "main.c"
                            "readings_config.c"
                            "readings_upload.c"
                    INCLUDE_DIRS "."
                    REQUIRES sim7000 modem_uart nvs_flash esp_app_format esp_timer
                             espressif__esp_modem esp_netif esp_event
                             esp_http_client mbedtls)
