set(main_srcs "main.c")
set(main_requires sim7000 modem_uart nvs_flash esp_app_format esp_timer
                  espressif__esp_modem esp_netif esp_event
                  esp_http_client mbedtls esp_wifi)

if(CONFIG_READINGS_UPLOAD_ENABLE STREQUAL "y")
    list(APPEND main_srcs "readings_config.c" "readings_upload.c")
endif()

# Wi-Fi STA session: lab SNTP and/or readings upload when cellular is off.
set(main_need_wifi_sta false)
if(CONFIG_LAB_WIFI_SNTP STREQUAL "y")
    set(main_need_wifi_sta true)
endif()
if(CONFIG_READINGS_UPLOAD_ENABLE STREQUAL "y" AND NOT CONFIG_APP_ENABLE_CELLULAR STREQUAL "y")
    set(main_need_wifi_sta true)
endif()
if(main_need_wifi_sta)
    list(APPEND main_srcs "wifi_lab_sntp.c")
endif()

idf_component_register(SRCS ${main_srcs}
                    INCLUDE_DIRS "."
                    REQUIRES ${main_requires})
