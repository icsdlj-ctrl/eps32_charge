#include "hs_weather.h"

/* Run this once after the application's existing NVS, Wi-Fi and SNTP
 * initialization. Does not replace Wi-Fi credentials or enable Wi-Fi. */
esp_err_t hs_weather_zhuhai_start(void)
{
    esp_err_t err = hs_weather_start();
    if (err != ESP_OK) return err;
    hs_weather_snapshot_t snapshot;
    err = hs_weather_get(&snapshot);
    if (err != ESP_OK) return err;
    /* Keep an already configured location and an intentional disabled setting. */
    if (snapshot.config.city[0]) return ESP_OK;
    const hs_weather_config_t config = {
        .version = 1,
        .latitude_e6 = 22276940,
        .longitude_e6 = 113567780,
        .city = "Zhuhai",
        .timezone = "Asia/Shanghai",
        .enabled = 1,
    };
    return hs_weather_configure(&config);
}
