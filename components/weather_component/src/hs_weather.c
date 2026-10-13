#include "hs_weather.h"
#include "hs_location.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define POLL_MS 5000
#define REFRESH_SECONDS 1800
#define STALE_SECONDS 3600
#define MIN_REQUEST_SECONDS 60
static SemaphoreHandle_t lock;
static TaskHandle_t worker;
static hs_weather_snapshot_t state;
static uint32_t generation;
static int64_t updated_us;

typedef struct {
    char *body;
    size_t used;
    size_t capacity;
    bool overflow;
    size_t limit;
} response_t;

static bool config_valid(const hs_weather_config_t *c)
{
    if (!c || (c->version != 1 && c->version != 2) || c->enabled > 1 || c->auto_location > 1 ||
        !memchr(c->city, 0, sizeof(c->city)) || !memchr(c->timezone, 0, sizeof(c->timezone)))
        return false;
    if (!c->enabled)
        return true;
    if (c->version == 2 && c->auto_location && !c->city[0])
        return true;
    if (!c->city[0] || !hs_weather_coordinates_valid(c->latitude_e6, c->longitude_e6) ||
        !hs_weather_timezone_valid(c->timezone))
        return false;
    for (const unsigned char *p = (const unsigned char *)c->city; *p; ++p)
        if (*p < 0x20 || *p == 0x7f)
            return false;
    return true;
}

static esp_err_t on_http(esp_http_client_event_t *event)
{
    response_t *r = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        size_t n = (size_t)event->data_len;
        if (n > r->limit - r->used) {
            r->overflow = true;
            return ESP_ERR_NO_MEM;
        }
        size_t needed = r->used + n + 1;
        if (needed > r->capacity) {
            size_t capacity = r->capacity * 2;
            if (capacity < needed)
                capacity = needed;
            if (capacity > r->limit + 1)
                capacity = r->limit + 1;
            char *body = realloc(r->body, capacity);
            if (!body) {
                r->overflow = true;
                return ESP_ERR_NO_MEM;
            }
            r->body = body;
            r->capacity = capacity;
        }
        memcpy(r->body + r->used, event->data, n);
        r->used += n;
        r->body[r->used] = 0;
    }
    return ESP_OK;
}

static void coordinate(char *out, size_t size, int32_t value)
{
    uint32_t absolute = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    snprintf(out, size, "%s%" PRIu32 ".%06" PRIu32, value < 0 ? "-" : "", absolute / 1000000,
             absolute % 1000000);
}

static esp_err_t fetch_body(const char *url, size_t limit, char **out, size_t *size)
{
    response_t response = {.body = malloc(1024), .capacity = 1024, .limit = limit};
    if (!response.body)
        return ESP_ERR_NO_MEM;
    response.body[0] = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_http,
        .user_data = &response,
        .disable_auto_redirect = true,
        .buffer_size = 1024,
        .user_agent = "HoloStation-Weather/0.1",
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = ESP_ERR_NO_MEM;
    if (client) {
        err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        if (response.overflow)
            err = ESP_ERR_INVALID_SIZE;
        if (err == ESP_OK && status != 200)
            err = ESP_FAIL;

        esp_http_client_cleanup(client);
    }
    if (err == ESP_OK) {
        *out = response.body;
        *size = response.used;
    } else
        free(response.body);
    return err;
}

static esp_err_t fetch(const hs_weather_config_t *c, hs_weather_forecast_t *out)
{
    char latitude[20], longitude[20], zone[192], url[768];
    coordinate(latitude, sizeof(latitude), c->latitude_e6);
    coordinate(longitude, sizeof(longitude), c->longitude_e6);
    size_t z = 0;
    for (const char *p = c->timezone; *p; ++p) {
        if (*p == '+') {
            memcpy(zone + z, "%2B", 3);
            z += 3;
        } else
            zone[z++] = *p;
    }
    zone[z] = 0;
    int len = snprintf(
        url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
        "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
        "&temperature_unit=celsius&wind_speed_unit=kmh&timezone=%s&forecast_days=3",
        latitude, longitude, zone);
    if (len < 0 || (size_t)len >= sizeof(url))
        return ESP_ERR_INVALID_SIZE;
    char *body = NULL;
    size_t size = 0;
    esp_err_t err = fetch_body(url, HS_WEATHER_JSON_LIMIT, &body, &size);
    if (err == ESP_OK && !hs_weather_parse(body, size, out))
        err = ESP_ERR_INVALID_RESPONSE;
    free(body);
    return err;
}
static esp_err_t locate(hs_weather_config_t *cfg, uint32_t revision)
{
    char *body = NULL;
    size_t size = 0;
    hs_location_t result;
    esp_err_t err =
        fetch_body("https://ipwho.is/?fields=success,city,latitude,longitude,timezone.id&lang=en",
                   4096, &body, &size);
    if (err == ESP_OK && !hs_location_parse(body, size, &result))
        err = ESP_ERR_INVALID_RESPONSE;
    free(body);
    if (err != ESP_OK)
        return err;
    hs_weather_config_t updated = *cfg;
    updated.latitude_e6 = result.latitude_e6;
    updated.longitude_e6 = result.longitude_e6;
    memset(updated.city, 0, sizeof(updated.city));
    memset(updated.timezone, 0, sizeof(updated.timezone));
    strcpy(updated.city, result.city);
    strcpy(updated.timezone, result.timezone);
    xSemaphoreTake(lock, portMAX_DELAY);
    if (generation != revision) {
        xSemaphoreGive(lock);
        return ESP_ERR_INVALID_STATE;
    }
    bool changed = memcmp(&state.config, &updated, sizeof(updated)) != 0;
    if (changed) {
        nvs_handle_t h;
        err = nvs_open("hs_weather", NVS_READWRITE, &h);
        if (err == ESP_OK) {
            err = nvs_set_blob(h, "location", &updated, sizeof(updated));
            if (err == ESP_OK)
                err = nvs_commit(h);
            nvs_close(h);
        }
        /* This session can still use the verified location if cache persistence fails. */
        state.config = updated;
        state.has_data = false;
        updated_us = 0;
    }
    state.location_cached = false;
    xSemaphoreGive(lock);
    *cfg = updated;
    return ESP_OK;
}

static void set_status(hs_weather_status_t status, uint32_t revision)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (generation == revision)
        state.status = status;
    xSemaphoreGive(lock);
}

static void weather_task(void *arg)
{
    (void)arg;
    uint32_t revision = UINT32_MAX;
    unsigned failures = 0;
    int64_t due_us = 0, last_request_us = -60000000;
    bool previously_online = false;
    for (;;) {
        uint32_t notification = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notification, pdMS_TO_TICKS(POLL_MS));
        hs_weather_config_t cfg;
        uint32_t current_revision;
        xSemaphoreTake(lock, portMAX_DELAY);
        cfg = state.config;
        current_revision = generation;
        xSemaphoreGive(lock);
        if (revision != current_revision) {
            revision = current_revision;
            due_us = 0;
            failures = 0;
            previously_online = false;
        }
        if (notification)
            due_us = 0;
        if (!cfg.enabled) {
            set_status(HS_WEATHER_UNCONFIGURED, revision);
            continue;
        }
        wifi_ap_record_t access_point;
        bool online = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK;
        if (!online) {
            set_status(HS_WEATHER_WAIT_NETWORK, revision);
            previously_online = false;
            continue;
        }
        if (!previously_online)
            due_us = 0;
        previously_online = true;
        /* The existing clock task must perform SNTP synchronization first. */
        if (time(NULL) < 1704067200) {
            set_status(HS_WEATHER_WAIT_CLOCK, revision);
            continue;
        }
        int64_t now_us = esp_timer_get_time();
        if (now_us < due_us || now_us - last_request_us < MIN_REQUEST_SECONDS * 1000000LL)
            continue;
        last_request_us = now_us;
        esp_err_t geo_error = ESP_OK;
        if (cfg.auto_location) {
            set_status(HS_WEATHER_LOCATING, revision);
            geo_error = locate(&cfg, revision);
            xSemaphoreTake(lock, portMAX_DELAY);
            if (generation == revision)
                state.location_cached = geo_error != ESP_OK && cfg.city[0];
            xSemaphoreGive(lock);
        }
        hs_weather_forecast_t forecast;
        esp_err_t err = geo_error;
        if (geo_error == ESP_OK || cfg.city[0]) {
            set_status(HS_WEATHER_FETCHING, revision);
            err = fetch(&cfg, &forecast);
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        if (generation == revision) {
            state.last_error = err;
            if (err == ESP_OK) {
                state.forecast = forecast;
                state.has_data = true;
                state.status = HS_WEATHER_READY;
                state.fetched_at_epoch = time(NULL);
                updated_us = esp_timer_get_time();
            } else
                state.status = HS_WEATHER_ERROR;
        }
        xSemaphoreGive(lock);
        if (err == ESP_OK) {
            failures = 0;
            due_us = esp_timer_get_time() + REFRESH_SECONDS * 1000000LL;
        } else {
            if (failures < 4)
                ++failures;
            unsigned delay = 60u << (failures - 1);
            due_us = esp_timer_get_time() + (int64_t)delay * 1000000;
        }
    }
}

esp_err_t hs_weather_start(void)
{
    if (worker)
        return ESP_OK;
    if (lock)
        return ESP_ERR_INVALID_STATE;
    lock = xSemaphoreCreateMutex();
    if (!lock)
        return ESP_ERR_NO_MEM;
    memset(&state, 0, sizeof(state));
    state.config.version = 1;
    state.status = HS_WEATHER_UNCONFIGURED;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("hs_weather", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        hs_weather_config_t saved = {0};
        size_t size = sizeof(saved);
        err = nvs_get_blob(handle, "location", &saved, &size);
        nvs_close(handle);
        if (err == ESP_OK && size == sizeof(saved) && config_valid(&saved))
            state.config = saved;
        else if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_OK)
            goto failed;
    } else if (err != ESP_ERR_NVS_NOT_FOUND)
        goto failed;
    if (xTaskCreate(weather_task, "hs_weather", 8192, NULL, 2, &worker) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto failed;
    }
    return ESP_OK;
failed:
    vSemaphoreDelete(lock);
    lock = NULL;
    return err;
}

esp_err_t hs_weather_configure(const hs_weather_config_t *config)
{
    if (!config_valid(config))
        return ESP_ERR_INVALID_ARG;
    if (!worker || !lock)
        return ESP_ERR_INVALID_STATE;
    hs_weather_config_t normalized = {0};
    normalized.version = config->version;
    normalized.latitude_e6 = config->latitude_e6;
    normalized.longitude_e6 = config->longitude_e6;
    normalized.enabled = config->enabled;
    normalized.auto_location = config->auto_location;
    memcpy(normalized.city, config->city, strlen(config->city) + 1);
    memcpy(normalized.timezone, config->timezone, strlen(config->timezone) + 1);
    xSemaphoreTake(lock, portMAX_DELAY);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("hs_weather", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "location", &normalized, sizeof(normalized));
        if (err == ESP_OK)
            err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err == ESP_OK) {
        memset(&state, 0, sizeof(state));
        state.config = normalized;
        state.status = normalized.enabled ? HS_WEATHER_WAIT_NETWORK : HS_WEATHER_UNCONFIGURED;
        updated_us = 0;
        ++generation;
    }
    xSemaphoreGive(lock);
    if (err == ESP_OK)
        xTaskNotify(worker, 1, eSetBits);
    return err;
}

esp_err_t hs_weather_get(hs_weather_snapshot_t *out)
{
    if (!out)
        return ESP_ERR_INVALID_ARG;
    if (!lock)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = state;
    if (out->has_data) {
        out->age_seconds = (uint32_t)((esp_timer_get_time() - updated_us) / 1000000);
        out->stale = out->age_seconds >= STALE_SECONDS || out->last_error != ESP_OK ||
                     out->location_cached || out->status == HS_WEATHER_WAIT_NETWORK ||
                     out->status == HS_WEATHER_WAIT_CLOCK;
    }
    xSemaphoreGive(lock);
    return ESP_OK;
}

void hs_weather_refresh(void)
{
    if (worker)
        xTaskNotify(worker, 1, eSetBits);
}
void hs_weather_relocate(void)
{
    if (worker)
        xTaskNotify(worker, 1, eSetBits);
}
