idf_component_register(SRCS "main.c" "ble_print.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash usb bt esp_http_server mdns led_strip)
