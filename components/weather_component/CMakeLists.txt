idf_component_register(
    SRCS "src/hs_weather_model.c" "src/hs_weather.c" "src/hs_location.c"
    INCLUDE_DIRS "include"
    REQUIRES json esp_http_client mbedtls esp_timer esp_wifi nvs_flash
)
