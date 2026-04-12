set(main_srcs "main.c"
               "readings_config.c"
               "readings_upload.c")
set(main_requires sim7000 modem_uart nvs_flash esp_app_format esp_timer
                  espressif__esp_modem esp_netif esp_event
                  esp_http_client mbedtls esp_wifi)

# Optional lab source; CONFIG_* is not reliable during CMake early expansion, so esp_wifi stays in
# REQUIRES whenever this component is registered (slightly larger image when lab SNTP is off).
if(CONFIG_LAB_WIFI_SNTP STREQUAL "y")
    list(APPEND main_srcs "wifi_lab_sntp.c")
endif()

idf_component_register(SRCS ${main_srcs}
                    INCLUDE_DIRS "."
                    REQUIRES ${main_requires})
