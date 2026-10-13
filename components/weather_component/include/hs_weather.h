#ifndef HS_WEATHER_H
#define HS_WEATHER_H
#include "hs_weather_model.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t version; /* 1: manual, 2: automatic IP geolocation */
    int32_t latitude_e6;
    int32_t longitude_e6;
    char city[48];
    char timezone[64]; /* IANA, e.g. Asia/Shanghai */
    uint8_t enabled;
    uint8_t auto_location;
    uint8_t reserved[2];
} hs_weather_config_t;
typedef enum {
    HS_WEATHER_UNCONFIGURED,
    HS_WEATHER_WAIT_NETWORK,
    HS_WEATHER_WAIT_CLOCK,
    HS_WEATHER_FETCHING,
    HS_WEATHER_READY,
    HS_WEATHER_ERROR,
    HS_WEATHER_LOCATING
} hs_weather_status_t;
typedef struct {
    hs_weather_config_t config;
    hs_weather_forecast_t forecast;
    hs_weather_status_t status;
    esp_err_t last_error;
    bool has_data;
    bool stale;
    bool location_cached;
    uint32_t age_seconds;
    int64_t fetched_at_epoch;
} hs_weather_snapshot_t;

/* Call once, from app startup, after existing NVS and Wi-Fi initialization.
 * Uses a new hs_weather namespace; never initializes or erases existing NVS. */
esp_err_t hs_weather_start(void);
esp_err_t hs_weather_configure(const hs_weather_config_t *config);
esp_err_t hs_weather_get(hs_weather_snapshot_t *out);
/* Manual refresh is rate limited to one request per minute. */
void hs_weather_refresh(void);
/* Re-check location after the Wi-Fi network changes. */
void hs_weather_relocate(void);

#ifdef __cplusplus
}
#endif
#endif
