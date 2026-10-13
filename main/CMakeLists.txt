idf_component_register(
    SRCS "main.c" "settings.c" "power.c" "charge_session.c" "display.c" "ui.c" "font.c" "network.c" "ble.c" "images.c" "usb.c"
    INCLUDE_DIRS "."
    REQUIRES weather_component esp_driver_gpio esp_driver_spi esp_driver_ledc
             esp_driver_i2c esp_lcd nvs_flash esp_wifi esp_netif esp_event
             esp_timer esp_http_server esp_https_ota app_update json bt spiffs
             mbedtls lwip esp_driver_usb_serial_jtag
    EMBED_TXTFILES "web.html"
)
