#include "hs_weather_model.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool shallow_json(const char *s, size_t n)
{
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0) return false;
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            else if (c < 0x20) return false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 24) return false;
        } else if (c == '}' || c == ']') {
            if (!depth) return false;
            --depth;
        }
    }
    return !quoted && depth == 0;
}

static const cJSON *field(const cJSON *obj, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(obj, name);
}

static bool number(const cJSON *j, double low, double high, double *out)
{
    if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble) ||
        j->valuedouble < low || j->valuedouble > high) return false;
    *out = j->valuedouble;
    return true;
}

static bool integer(const cJSON *j, int low, int high, int *out)
{
    double n;
    if (!number(j, low, high, &n) || n != (int)n) return false;
    *out = (int)n;
    return true;
}

static bool optional_percent(const cJSON *j, int *out)
{
    if (j == NULL || cJSON_IsNull(j)) { *out = -1; return true; }
    return integer(j, 0, 100, out);
}

static bool date_valid(const char *s)
{
    static const int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    int y = 0, m = 0, d = 0;
    if (!s || strlen(s) < 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return false;
    for (int i = 0; i < 4; ++i) y = y * 10 + s[i] - '0';
    m = (s[5]-'0')*10 + s[6]-'0';
    d = (s[8]-'0')*10 + s[9]-'0';
    if (y < 2000 || y > 2199 || m < 1 || m > 12) return false;
    int maxday = days[m] + (m == 2 && y%4 == 0 && (y%100 != 0 || y%400 == 0));
    return d >= 1 && d <= maxday;
}

static bool time_valid(const char *s)
{
    if (!s) return false;
    size_t n = strlen(s);
    if ((n != 16 && n != 19) || !date_valid(s) || s[10] != 'T' || s[13] != ':') return false;
    const int indexes[] = {11,12,14,15};
    for (int i = 0; i < 4; ++i)
        if (s[indexes[i]] < '0' || s[indexes[i]] > '9') return false;
    if ((s[11]-'0')*10 + s[12]-'0' > 23 || (s[14]-'0')*10 + s[15]-'0' > 59) return false;
    if (n == 19 && (s[16] != ':' || s[17] < '0' || s[17] > '5' || s[18] < '0' || s[18] > '9')) return false;
    return true;
}

static bool unit(const cJSON *obj, const char *name, const char *expected)
{
    const cJSON *j = field(obj, name);
    return cJSON_IsString(j) && strcmp(j->valuestring, expected) == 0;
}

static bool next_date(const char *previous, const char *next)
{
    int y, m, d;
    static const int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (sscanf(previous,"%d-%d-%d",&y,&m,&d) != 3) return false;
    int maxday = days[m] + (m == 2 && y%4 == 0 && (y%100 != 0 || y%400 == 0));
    if (++d > maxday) { d = 1; if (++m > 12) { m = 1; ++y; } }
    char expected[11];
    snprintf(expected, sizeof(expected), "%04d-%02d-%02d", y, m, d);
    return strcmp(expected,next) == 0;
}

bool hs_weather_parse(const char *body, size_t size, hs_weather_forecast_t *out)
{
    if (!body || !out || !size || size > HS_WEATHER_JSON_LIMIT || !shallow_json(body,size)) return false;
    char *copy = malloc(size + 1);
    if (!copy) return false;
    memcpy(copy,body,size); copy[size] = 0;
    cJSON *root = cJSON_ParseWithOpts(copy,NULL,1);
    free(copy);
    if (!root) return false;
    hs_weather_forecast_t result = {0};
    bool ok = false;
    const cJSON *cur = field(root,"current"), *daily = field(root,"daily");
    const cJSON *cu = field(root,"current_units"), *du = field(root,"daily_units");
    const cJSON *at = field(cur,"time");
    if (!cJSON_IsObject(cur) || !cJSON_IsObject(daily) || !cJSON_IsString(at) || !time_valid(at->valuestring)) goto done;
    if (!unit(cu,"temperature_2m","\xc2\xb0" "C") ||
        !unit(cu,"wind_speed_10m","km/h") || !unit(cu,"relative_humidity_2m","%") ||
        !unit(du,"temperature_2m_min","\xc2\xb0" "C") || !unit(du,"temperature_2m_max","\xc2\xb0" "C") ||
        !unit(du,"precipitation_probability_max","%")) goto done;
    if (!number(field(cur,"temperature_2m"),-100,100,&result.temperature_c) ||
        !number(field(cur,"wind_speed_10m"),0,500,&result.wind_kmh) ||
        !integer(field(cur,"weather_code"),0,99,&result.weather_code) ||
        !optional_percent(field(cur,"relative_humidity_2m"),&result.humidity)) goto done;
    memcpy(result.observed_at,at->valuestring,strlen(at->valuestring)+1);
    const cJSON *dates = field(daily,"time"), *mins = field(daily,"temperature_2m_min");
    const cJSON *maxs = field(daily,"temperature_2m_max"), *codes = field(daily,"weather_code");
    const cJSON *rains = field(daily,"precipitation_probability_max");
    const cJSON *arrays[] = {dates,mins,maxs,codes,rains};
    for (int j = 0; j < 5; ++j)
        if (!cJSON_IsArray(arrays[j]) || cJSON_GetArraySize(arrays[j]) != HS_WEATHER_DAYS) goto done;
    for (int i = 0; i < HS_WEATHER_DAYS; ++i) {
        hs_weather_day_t *d = &result.days[i];
        const cJSON *date = cJSON_GetArrayItem(dates,i);
        if (!cJSON_IsString(date) || strlen(date->valuestring) != 10 || !date_valid(date->valuestring)) goto done;
        memcpy(d->date,date->valuestring,11);
        if (!number(cJSON_GetArrayItem(mins,i),-100,100,&d->minimum_c) ||
            !number(cJSON_GetArrayItem(maxs,i),-100,100,&d->maximum_c) || d->minimum_c > d->maximum_c ||
            !integer(cJSON_GetArrayItem(codes,i),0,99,&d->weather_code) ||
            !optional_percent(cJSON_GetArrayItem(rains,i),&d->rain_probability)) goto done;
        if (i == 0 && memcmp(d->date,result.observed_at,10) != 0) goto done;
        if (i > 0 && !next_date(result.days[i-1].date,d->date)) goto done;
    }
    *out = result;
    ok = true;
done:
    cJSON_Delete(root);
    return ok;
}

bool hs_weather_coordinates_valid(int32_t lat, int32_t lon)
{
    return lat >= -90000000 && lat <= 90000000 && lon >= -180000000 && lon <= 180000000;
}

bool hs_weather_timezone_valid(const char *zone)
{
    if (!zone) return false;
    size_t n = strlen(zone);
    if (!n || n >= 64) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = zone[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '+')) return false;
    }
    return true;
}

const char *hs_weather_condition(int c)
{
    switch(c) {
    case 0: return "Clear";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: case 63: case 65: return "Rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Rain showers";
    case 85: case 86: return "Snow showers";
    case 95: case 96: case 99: return "Thunderstorm";
    default: return "Unknown";
    }
}
