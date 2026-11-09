#include "station.h"
#include "hs_weather.h"
#include "device_storage.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <time.h>

void app_main(void)
{
    ESP_LOGI("MAIN", "Holo weather station %s", STATION_VERSION);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGW("MAIN", "nvs has no free pages; calibration area will be reinitialized");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(storage_init());
    setenv("TZ", "CST-8", 1);
    tzset();
    station_init_state();
    ESP_ERROR_CHECK(station_load_settings());
    ESP_ERROR_CHECK(station_images_init());
    ESP_ERROR_CHECK(station_start_power());
    if (station_catch_boot_stop()) {
        station_status("应用已停止");
        ESP_LOGW("MAIN", "application held; Wi-Fi stays off until the USB tool resumes it");
        if (station_start_display() != ESP_OK)
            ESP_LOGW("MAIN", "display unavailable while application is held");
    } else {
        ESP_ERROR_CHECK(station_start_network());
        ESP_ERROR_CHECK(hs_weather_start());
        hs_weather_snapshot_t weather;
        ESP_ERROR_CHECK(hs_weather_get(&weather));
        if (!weather.config.enabled) {
            const hs_weather_config_t config = {.version = 2, .enabled = 1, .auto_location = 1};
            ESP_ERROR_CHECK(hs_weather_configure(&config));
        }
        if (station_start_display() != ESP_OK)
            ESP_LOGW("MAIN", "display unavailable; Wi-Fi setup server continues");
        ESP_ERROR_CHECK(station_start_ble());
    }
    ESP_ERROR_CHECK(station_start_usb());
    station_status("Ready");
    ESP_LOGI("MAIN", "all tasks started, heap %lu", (unsigned long)esp_get_free_heap_size());
}
