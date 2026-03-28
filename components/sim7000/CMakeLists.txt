idf_component_register(SRCS "sim7000.c" "modem_config.c"
                    INCLUDE_DIRS "include"
                    REQUIRES at_client nvs_flash)
