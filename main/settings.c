#include "station.h"
#include "hs_weather.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static SemaphoreHandle_t state_lock;
static SemaphoreHandle_t command_lock;
static station_snapshot_t state;
static const char *keys[] = {"brightness", "nightBright", "nightModeIsOn", "skinKind", "maxPower",
                             "startTime",  "stopTime",    "timeThreshold", "page"};
void station_init_state(void)
{
    state_lock = xSemaphoreCreateMutex();
    command_lock = xSemaphoreCreateMutex();
    if (!state_lock || !command_lock)
        abort();
    state.config = (station_config_t){.brightness = 35,
                                      .night_brightness = 8,
                                      .max_power = 300,
                                      .night_start = 2200,
                                      .night_stop = 700,
                                      .timeout = 3,
                                      .page = PAGE_AUTO};
    strcpy(state.status, "Starting");
}
void station_snapshot(station_snapshot_t *out)
{
    xSemaphoreTake(state_lock, portMAX_DELAY);
    *out = state;
    xSemaphoreGive(state_lock);
}
void station_update_port(int i, const station_port_t *p)
{
    if (i < 0 || i >= 4)
        return;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    state.ports[i] = *p;
    xSemaphoreGive(state_lock);
}
void station_network_state(bool connected, const char *ip)
{
    xSemaphoreTake(state_lock, portMAX_DELAY);
    state.wifi_connected = connected;
    snprintf(state.ip, sizeof(state.ip), "%s", ip ? ip : "");
    xSemaphoreGive(state_lock);
}
void station_status(const char *text)
{
    xSemaphoreTake(state_lock, portMAX_DELAY);
    snprintf(state.status, sizeof(state.status), "%s", text);
    xSemaphoreGive(state_lock);
}
void station_ota_state(bool busy, uint8_t progress, const char *text)
{
    xSemaphoreTake(state_lock, portMAX_DELAY);
    state.ota_busy = busy;
    state.ota_progress = progress;
    snprintf(state.status, sizeof(state.status), "%s", text);
    xSemaphoreGive(state_lock);
}

bool station_night_window(unsigned now, unsigned start, unsigned stop)
{
    unsigned n = (now / 100) * 60 + now % 100, a = (start / 100) * 60 + start % 100,
             b = (stop / 100) * 60 + stop % 100;
    return a < b ? n >= a && n < b : n >= a || n < b;
}
static bool hhmm(int n)
{
    return n >= 0 && n <= 2359 && n % 100 < 60;
}
esp_err_t station_load_settings(void)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open("devConfig", NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (e != ESP_OK)
        return e;
    station_config_t c = state.config;
    nvs_get_u8(h, keys[0], &c.brightness);
    nvs_get_u8(h, keys[1], &c.night_brightness);
    nvs_get_u8(h, keys[2], &c.night_enabled);
    nvs_get_u8(h, keys[3], &c.skin);
    nvs_get_u16(h, keys[4], &c.max_power);
    nvs_get_u16(h, keys[5], &c.night_start);
    nvs_get_u16(h, keys[6], &c.night_stop);
    nvs_get_u16(h, keys[7], &c.timeout);
    nvs_get_u8(h, keys[8], &c.page);
    nvs_close(h);
    if (c.brightness > 100)
        c.brightness = 35;
    if (c.night_brightness > 100)
        c.night_brightness = 8;
    if (c.night_enabled > 1)
        c.night_enabled = 0;
    if (c.skin > 3)
        c.skin = 0;
    if (c.max_power < 1 || c.max_power > 1000)
        c.max_power = 300;
    if (!hhmm(c.night_start))
        c.night_start = 2200;
    if (!hhmm(c.night_stop))
        c.night_stop = 700;
    if (c.timeout > 3600)
        c.timeout = 60;
    if (c.page > 3)
        c.page = 0;
    state.config = c;
    return ESP_OK;
}
static esp_err_t save_config(const station_config_t *c)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open("devConfig", NVS_READWRITE, &h);
    if (e != ESP_OK)
        return e;
    const uint8_t small[] = {c->brightness, c->night_brightness, c->night_enabled, c->skin};
    for (int i = 0; i < 4 && e == ESP_OK; i++)
        e = nvs_set_u8(h, keys[i], small[i]);
    const uint16_t large[] = {c->max_power, c->night_start, c->night_stop, c->timeout};
    for (int i = 0; i < 4 && e == ESP_OK; i++)
        e = nvs_set_u16(h, keys[i + 4], large[i]);
    if (e == ESP_OK)
        e = nvs_set_u8(h, keys[8], c->page);
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    return e;
}
static bool get_int(const cJSON *o, const char *key, int min, int max, int *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsBool(v)) {
        *out = cJSON_IsTrue(v);
        return *out >= min && *out <= max;
    }
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < min ||
        v->valuedouble > max || v->valuedouble != (int)v->valuedouble)
        return false;
    *out = (int)v->valuedouble;
    return true;
}
static esp_err_t apply_json(const char *text, size_t length)
{
    if (!text || length < 2 || length > 4096 || memchr(text, 0, length))
        return ESP_ERR_INVALID_ARG;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = text[i];
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 24)
                return ESP_ERR_INVALID_ARG;
        } else if (c == '}' || c == ']') {
            if (!depth)
                return ESP_ERR_INVALID_ARG;
            --depth;
        }
    }
    if (depth || quoted)
        return ESP_ERR_INVALID_ARG;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(text, length, &end, false);
    if (end)
        while (end < text + length && isspace((unsigned char)*end))
            end++;
    if (!cJSON_IsObject(root) || end != text + length) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ESP_OK;
    bool changed = false, handled = false;
    station_snapshot_t s;
    station_snapshot(&s);
    station_config_t c = s.config;
    const cJSON *o;
    int v;
#define INT_COMMAND(name, member, low, high)                                                       \
    do {                                                                                           \
        o = cJSON_GetObjectItemCaseSensitive(root, name);                                          \
        if (o) {                                                                                   \
            handled = true;                                                                        \
            if (!get_int(o, "value", low, high, &v)) {                                             \
                err = ESP_ERR_INVALID_ARG;                                                         \
                goto done;                                                                         \
            }                                                                                      \
            c.member = v;                                                                          \
            changed = true;                                                                        \
        }                                                                                          \
    } while (0)
    INT_COMMAND("setBright", brightness, 0, 100);
    INT_COMMAND("setMaxPower", max_power, 1, 1000);
    INT_COMMAND("setSkinKind", skin, 0, 3);
    INT_COMMAND("setTimeOut", timeout, 0, 3600);
    INT_COMMAND("setPage", page, 0, 3);
    o = cJSON_GetObjectItemCaseSensitive(root, "setNight");
    if (o) {
        handled = true;
        int enabled, bright, start, stop;
        if (!get_int(o, "nightModeIsOn", 0, 1, &enabled) ||
            !get_int(o, "nightBright", 0, 100, &bright) ||
            !get_int(o, "startTime", 0, 2359, &start) || !get_int(o, "stopTime", 0, 2359, &stop) ||
            !hhmm(start) || !hhmm(stop)) {
            err = ESP_ERR_INVALID_ARG;
            goto done;
        }
        c.night_enabled = enabled;
        c.night_brightness = bright;
        c.night_start = start;
        c.night_stop = stop;
        changed = true;
    }
    if (changed) {
        xSemaphoreTake(state_lock, portMAX_DELAY);
        err = save_config(&c);
        if (err == ESP_OK)
            state.config = c;
        xSemaphoreGive(state_lock);
        if (err != ESP_OK)
            goto done;
    }
    o = cJSON_GetObjectItemCaseSensitive(root, "setWiFi");
    if (o) {
        handled = true;
        const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(o, "ssid"),
                    *pw = cJSON_GetObjectItemCaseSensitive(o, "psw");
        if (!pw)
            pw = cJSON_GetObjectItemCaseSensitive(o, "password");
        if (!cJSON_IsString(ssid) || !cJSON_IsString(pw)) {
            err = ESP_ERR_INVALID_ARG;
            goto done;
        }
        err = station_save_wifi(ssid->valuestring, pw->valuestring);
        if (err != ESP_OK)
            goto done;
    }
    o = cJSON_GetObjectItemCaseSensitive(root, "setImage");
    if (o) {
        handled = true;
        err = station_image_packet(o);
        if (err != ESP_OK)
            goto done;
    }
    o = cJSON_GetObjectItemCaseSensitive(root, "setOTA");
    if (o) {
        handled = true;
        int begin;
        if (!get_int(o, "isBeginOTA", 0, 1, &begin)) {
            err = ESP_ERR_INVALID_ARG;
            goto done;
        }
        if (begin) {
            const cJSON *url = cJSON_GetObjectItemCaseSensitive(o, "url");
            if (!cJSON_IsString(url)) {
                err = ESP_ERR_INVALID_ARG;
                goto done;
            }
            err = station_start_ota(url->valuestring);
        }
    }
    o = cJSON_GetObjectItemCaseSensitive(root, "refreshWeather");
    if (o) {
        handled = true;
        hs_weather_refresh();
    }
    o = cJSON_GetObjectItemCaseSensitive(root, "setActive");
    if (o) {
        handled = true; /* New firmware has no vendor activation requirement. */
    }
    if (!handled)
        err = ESP_ERR_NOT_SUPPORTED;
done:
    cJSON_Delete(root);
    return err;
}
esp_err_t station_apply_json(const char *text, size_t length)
{
    xSemaphoreTake(command_lock, portMAX_DELAY);
    esp_err_t e = apply_json(text, length);
    xSemaphoreGive(command_lock);
    return e;
}
/* Use integer formatting: newlib float printf needs a large stack on C3. */
static void decimal(char *out, size_t size, float value, unsigned digits)
{
    int64_t factor = digits == 3 ? 1000 : 100;
    if (!isfinite(value) || fabsf(value) > 100000)
        value = 0;
    int64_t v = (int64_t)llroundf(value * factor);
    uint64_t absolute = v < 0 ? (uint64_t)-v : (uint64_t)v;
    snprintf(out, size, "%s%llu.%0*llu", v < 0 ? "-" : "", (unsigned long long)(absolute / factor),
             (int)digits, (unsigned long long)(absolute % factor));
}
size_t station_json(char *out, size_t size)
{
    station_snapshot_t s;
    station_snapshot(&s);
    const char *names[] = {"A1", "C1", "C2", "C3"};
    size_t used = 0;
    for (int i = 0; i < 4; i++) {
        char v[24], current[24], power[24];
        decimal(v, sizeof(v), s.ports[i].voltage, 2);
        decimal(current, sizeof(current), s.ports[i].current, 3);
        decimal(power, sizeof(power), s.ports[i].power, 2);
        int n =
            snprintf(out + used, size - used, "%s\"%s\":{\"V\":%s,\"I\":%s,\"P\":%s,\"T\":%lu},",
                     i ? "" : "{", names[i], v, current, power, (unsigned long)s.ports[i].seconds);
        if (n < 0 || (size_t)n >= size - used)
            return 0;
        used += n;
    }
    int n = snprintf(
        out + used, size - used,
        "\"config\":{\"Ver\":\"%s\",\"wifiState\":%d,\"bright\":%d,\"maxPower\":%d,"
        "\"nightModeIsOn\":%d,\"nightBright\":%d,\"startTime\":%d,\"stopTime\":%d,\"imgState\":%d,"
        "\"timeOut\":%d,\"isActivated\":1,\"page\":%d}}",
        STATION_VERSION, s.wifi_connected ? 2 : 0, s.config.brightness, s.config.max_power,
        s.config.night_enabled, s.config.night_brightness, s.config.night_start,
        s.config.night_stop, s.config.skin, s.config.timeout, s.config.page);
    return n > 0 && (size_t)n < size - used ? used + n : 0;
}
