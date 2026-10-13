#ifndef HS_WEATHER_MODEL_H
#define HS_WEATHER_MODEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HS_WEATHER_DAYS 3
#define HS_WEATHER_JSON_LIMIT 16384
typedef struct {
    char date[11];
    double minimum_c;
    double maximum_c;
    int weather_code;
    int rain_probability; /* -1 means missing */
} hs_weather_day_t;
typedef struct {
    char observed_at[20];
    double temperature_c;
    double wind_kmh;
    int humidity; /* -1 means missing */
    int weather_code;
    hs_weather_day_t days[HS_WEATHER_DAYS];
} hs_weather_forecast_t;

/* On failure, out is unchanged. body need not have a NUL terminator. */
bool hs_weather_parse(const char *body, size_t size,
                      hs_weather_forecast_t *out);
const char *hs_weather_condition(int code);
bool hs_weather_timezone_valid(const char *zone);
bool hs_weather_coordinates_valid(int32_t latitude_e6, int32_t longitude_e6);

#ifdef __cplusplus
}
#endif
#endif
