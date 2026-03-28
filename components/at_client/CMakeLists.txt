idf_component_register(SRCS "at_client.c"
                    INCLUDE_DIRS "include"
                    REQUIRES modem_uart freertos esp_timer)
