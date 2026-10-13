#include "hs_location.h"
#include "hs_weather_model.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>

static bool bounded_json(const char *s, size_t n)
{
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = s[i];
        if (!c)
            return false;
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
            else if (c < 32)
                return false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 12)
                return false;
        } else if (c == '}' || c == ']') {
            if (!depth)
                return false;
            depth--;
        }
    }
    return !quoted && depth == 0;
}
bool hs_location_parse(const char *body, size_t size, hs_location_t *out)
{
    if (!body || !out || size < 2 || size > 4096 || !bounded_json(body, size))
        return false;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(body, size, &end, false);
    if (!root)
        return false;
    while (end < body + size && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t'))
        end++;
    bool ok = false;
    hs_location_t result = {0};
    const cJSON *success = cJSON_GetObjectItemCaseSensitive(root, "success"),
                *city = cJSON_GetObjectItemCaseSensitive(root, "city"),
                *lat = cJSON_GetObjectItemCaseSensitive(root, "latitude"),
                *lon = cJSON_GetObjectItemCaseSensitive(root, "longitude"),
                *tz = cJSON_GetObjectItemCaseSensitive(
                    cJSON_GetObjectItemCaseSensitive(root, "timezone"), "id");
    if (end != body + size || !cJSON_IsTrue(success) || !cJSON_IsString(city) ||
        !cJSON_IsString(tz) || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) ||
        !isfinite(lat->valuedouble) || !isfinite(lon->valuedouble) || lat->valuedouble < -90 ||
        lat->valuedouble > 90 || lon->valuedouble < -180 || lon->valuedouble > 180 ||
        !city->valuestring[0] || strlen(city->valuestring) >= sizeof(result.city) ||
        strlen(tz->valuestring) >= sizeof(result.timezone) ||
        !hs_weather_timezone_valid(tz->valuestring))
        goto done;
    for (const unsigned char *p = (const unsigned char *)city->valuestring; *p; p++)
        if (*p < 32 || *p == 127)
            goto done;
    result.latitude_e6 = (int32_t)llround(lat->valuedouble * 1000000);
    result.longitude_e6 = (int32_t)llround(lon->valuedouble * 1000000);
    memcpy(result.city, city->valuestring, strlen(city->valuestring) + 1);
    memcpy(result.timezone, tz->valuestring, strlen(tz->valuestring) + 1);
    *out = result;
    ok = true;
done:
    cJSON_Delete(root);
    return ok;
}
