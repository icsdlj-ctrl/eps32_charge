#include "station.h"
#include "hs_weather.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static SemaphoreHandle_t ota_lock;
static volatile bool configured;
static esp_err_t json_response(httpd_req_t *req, const char *body)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (configured)
            esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        station_network_state(false, NULL);
        wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW("NET", "Wi-Fi disconnected reason %u", event->reason);
        if (configured)
            esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        station_network_state(true, ip);
        station_status("Connected");
        ESP_LOGI("NET", "station IP %s", ip);
        hs_weather_relocate();
    }
}
esp_err_t station_save_wifi(const char *ssid, const char *password)
{
    if (!ssid || !password || strlen(ssid) < 1 || strlen(ssid) > 32 || strlen(password) > 63 ||
        (strlen(password) > 0 && strlen(password) < 8))
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open("WiFi_cfg", NVS_READWRITE, &h);
    if (e != ESP_OK)
        return e;
    e = nvs_set_str(h, "wifi_ssid", ssid);
    if (e == ESP_OK)
        e = nvs_set_str(h, "wifi_passwd", password);
    if (e == ESP_OK)
        e = nvs_set_u8(h, "wifi_state", 1);
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK)
        return e;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    config.sta.pmf_cfg.capable = true;
    configured = false;
    esp_wifi_disconnect();
    /* Let the disconnect event run before enabling retries for the new SSID. */
    vTaskDelay(pdMS_TO_TICKS(50));
    e = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (e == ESP_OK) {
        configured = true;
        e = esp_wifi_connect();
    }
    return e;
}
static bool load_wifi(wifi_config_t *cfg)
{
    nvs_handle_t h;
    char ssid[33] = {0}, password[64] = {0};
    size_t n = sizeof(ssid), m = sizeof(password);
    esp_err_t e = nvs_open("WiFi_cfg", NVS_READONLY, &h);
    if (e == ESP_OK) {
        e = nvs_get_str(h, "wifi_ssid", ssid, &n);
        if (e == ESP_OK)
            e = nvs_get_str(h, "wifi_passwd", password, &m);
        nvs_close(h);
    }
    if (e != ESP_OK) {
        n = sizeof(ssid);
        m = sizeof(password);
        e = nvs_open("station", NVS_READONLY, &h);
        if (e == ESP_OK) {
            e = nvs_get_str(h, "ssid", ssid, &n);
            if (e == ESP_OK)
                e = nvs_get_str(h, "password", password, &m);
            nvs_close(h);
        }
    }
    if (e != ESP_OK || !ssid[0])
        return false;
    memcpy(cfg->sta.ssid, ssid, strlen(ssid));
    memcpy(cfg->sta.password, password, strlen(password));
    cfg->sta.pmf_cfg.capable = true;
    return true;
}
static esp_err_t home(httpd_req_t *req)
{
    extern const char web_start[] asm("_binary_web_html_start");
    extern const char web_end[] asm("_binary_web_html_end");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, web_start, web_end - web_start - 1);
}
char *station_status_json(void)
{
    char station[640];
    size_t n = station_json(station, sizeof(station));
    if (!n)
        return NULL;
    cJSON *root = cJSON_Parse(station);
    if (!root)
        return NULL;
    station_snapshot_t s;
    station_snapshot(&s);
    cJSON_AddStringToObject(root, "ip", s.ip);
    cJSON_AddStringToObject(root, "status", s.status);
    cJSON_AddBoolToObject(root, "otaBusy", s.ota_busy);
    cJSON_AddNumberToObject(root, "freeHeap", esp_get_free_heap_size());
    for (int i = 0; i < 4; i++) {
        const char *names[] = {"A1", "C1", "C2", "C3"};
        cJSON_AddBoolToObject(cJSON_GetObjectItem(root, names[i]), "valid", s.ports[i].valid);
        cJSON_AddNumberToObject(cJSON_GetObjectItem(root, names[i]), "mAh", s.ports[i].charge_mah);
        cJSON_AddNumberToObject(cJSON_GetObjectItem(root, names[i]), "Wh", s.ports[i].energy_wh);
    }
    hs_weather_snapshot_t w;
    if (hs_weather_get(&w) == ESP_OK) {
        cJSON *weather = cJSON_AddObjectToObject(root, "weather");
        cJSON_AddBoolToObject(weather, "valid", w.has_data);
        cJSON_AddBoolToObject(weather, "stale", w.stale);
        cJSON_AddStringToObject(weather, "city", w.config.city[0] ? w.config.city : "IP定位");
        cJSON_AddBoolToObject(weather, "autoLocation", w.config.auto_location);
        cJSON_AddBoolToObject(weather, "locationCached", w.location_cached);
        cJSON_AddStringToObject(weather, "timezone", w.config.timezone);
        cJSON_AddNumberToObject(weather, "state", w.status);
        if (w.has_data) {
            cJSON_AddNumberToObject(weather, "temperature", w.forecast.temperature_c);
            cJSON_AddStringToObject(weather, "condition",
                                    hs_weather_condition(w.forecast.weather_code));
            cJSON_AddNumberToObject(weather, "humidity", w.forecast.humidity);
            cJSON_AddNumberToObject(weather, "wind", w.forecast.wind_kmh);
            cJSON_AddNumberToObject(weather, "ageSeconds", w.age_seconds);
            cJSON *days = cJSON_AddArrayToObject(weather, "days");
            for (int i = 0; i < 3; i++) {
                cJSON *day = cJSON_CreateObject();
                cJSON_AddStringToObject(day, "date", w.forecast.days[i].date);
                cJSON_AddNumberToObject(day, "min", w.forecast.days[i].minimum_c);
                cJSON_AddNumberToObject(day, "max", w.forecast.days[i].maximum_c);
                cJSON_AddNumberToObject(day, "rain", w.forecast.days[i].rain_probability);
                cJSON_AddItemToArray(days, day);
            }
        }
    }
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}
static esp_err_t status(httpd_req_t *req)
{
    char *body = station_status_json();
    if (!body)
        return httpd_resp_send_500(req);
    esp_err_t e = json_response(req, body);
    free(body);
    return e;
}
static esp_err_t configure(httpd_req_t *req)
{
    if (req->content_len < 2 || req->content_len > 4096) {
        httpd_resp_set_status(req, "400 Bad Request");
        return json_response(req, "{\"error\":\"invalid length\"}");
    }
    char *body = malloc(req->content_len + 1);
    if (!body)
        return httpd_resp_send_500(req);
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) {
            free(body);
            return httpd_resp_send_408(req);
        }
        received += n;
    }
    body[received] = 0;
    esp_err_t e = station_apply_json(body, received);
    free(body);
    if (e != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        char message[96];
        snprintf(message, sizeof(message), "{\"error\":\"%s\"}", esp_err_to_name(e));
        return json_response(req, message);
    }
    return json_response(req, "{\"ok\":true}");
}
static esp_err_t image_frame(httpd_req_t *req)
{
    uint8_t b[1024];
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    for (size_t offset = 0; offset < LCD_WIDTH * LCD_HEIGHT * 2; offset += sizeof(b)) {
        size_t n = LCD_WIDTH * LCD_HEIGHT * 2 - offset;
        if (n > sizeof(b))
            n = sizeof(b);
        if (!station_copy_frame(b, offset, n))
            return ESP_FAIL;
        esp_err_t e = httpd_resp_send_chunk(req, (const char *)b, n);
        if (e != ESP_OK)
            return e;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}
static bool correct_app(const esp_app_desc_t *app)
{
    return app->magic_word == ESP_APP_DESC_MAGIC_WORD &&
           memchr(app->project_name, 0, sizeof(app->project_name)) &&
           strcmp(app->project_name, "holo_weather_station") == 0;
}
static esp_err_t upload_ota(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part || req->content_len < 288 || (size_t)req->content_len > part->size ||
        xSemaphoreTake(ota_lock, 0) != pdTRUE) {
        httpd_resp_set_status(req, "400 Bad Request");
        return json_response(req, "{\"error\":\"invalid image size or OTA busy\"}");
    }
    uint8_t buffer[1024];
    int initial = 0;
    esp_err_t err = ESP_OK;
    esp_ota_handle_t handle = 0;
    bool begun = false;
    while (initial < 288) {
        int n = httpd_req_recv(req, (char *)buffer + initial, 288 - initial);
        if (n <= 0) {
            err = ESP_FAIL;
            goto finish;
        }
        initial += n;
    }
    esp_app_desc_t descriptor;
    memcpy(&descriptor, buffer + 32, sizeof(descriptor));
    if (buffer[0] != 0xe9 || !correct_app(&descriptor)) {
        err = ESP_ERR_INVALID_ARG;
        goto finish;
    }
    station_ota_state(true, 0, "OTA upload");
    err = esp_ota_begin(part, req->content_len, &handle);
    if (err != ESP_OK)
        goto finish;
    begun = true;
    err = esp_ota_write(handle, buffer, initial);
    if (err != ESP_OK)
        goto finish;
    size_t received = initial;
    while (received < (size_t)req->content_len) {
        size_t take = req->content_len - received;
        if (take > sizeof(buffer))
            take = sizeof(buffer);
        int n = httpd_req_recv(req, (char *)buffer, take);
        if (n <= 0) {
            err = ESP_FAIL;
            goto finish;
        }
        err = esp_ota_write(handle, buffer, n);
        if (err != ESP_OK)
            goto finish;
        received += n;
        station_ota_state(true, (uint8_t)(received * 100 / req->content_len), "OTA upload");
    }
    err = esp_ota_end(handle);
    begun = false;
    if (err == ESP_OK)
        err = esp_ota_set_boot_partition(part);
finish:
    if (begun)
        esp_ota_abort(handle);
    station_ota_state(false, err == ESP_OK ? 100 : 0,
                      err == ESP_OK ? "OTA ready; reboot" : "OTA rejected");
    xSemaphoreGive(ota_lock);
    if (err == ESP_OK) {
        json_response(req, "{\"ok\":true,\"reboot\":true}");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    httpd_resp_set_status(req, "400 Bad Request");
    return json_response(req, "{\"error\":\"OTA failed; upload this project's app.bin\"}");
}
static void ota_task(void *arg)
{
    char *url = arg;
    esp_https_ota_handle_t handle = NULL;
    esp_err_t err;
    esp_http_client_config_t http = {.url = url,
                                     .crt_bundle_attach = esp_crt_bundle_attach,
                                     .timeout_ms = 15000,
                                     .keep_alive_enable = true};
    esp_https_ota_config_t config = {.http_config = &http};
    station_ota_state(true, 0, "OTA download");
    err = esp_https_ota_begin(&config, &handle);
    if (err == ESP_OK) {
        esp_app_desc_t descriptor;
        err = esp_https_ota_get_img_desc(handle, &descriptor);
        if (err == ESP_OK && !correct_app(&descriptor))
            err = ESP_ERR_INVALID_ARG;
        while (err == ESP_OK) {
            err = esp_https_ota_perform(handle);
            int size = esp_https_ota_get_image_size(handle),
                read = esp_https_ota_get_image_len_read(handle);
            if (size > 0)
                station_ota_state(true, (uint8_t)(read * 100LL / size), "OTA download");
            if (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                err = ESP_OK;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            break;
        }
        if (err == ESP_OK)
            err = esp_https_ota_finish(handle);
        else
            esp_https_ota_abort(handle);
    }
    free(url);
    station_ota_state(false, err == ESP_OK ? 100 : 0, err == ESP_OK ? "OTA ready" : "OTA failed");
    xSemaphoreGive(ota_lock);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    vTaskDelete(NULL);
}
esp_err_t station_start_ota(const char *url)
{
    if (!url || strlen(url) > 768 || strncmp(url, "https://", 8))
        return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(ota_lock, 0) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    char *copy = strdup(url);
    if (!copy) {
        xSemaphoreGive(ota_lock);
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(ota_task, "ota", 8192, copy, 3, NULL) != pdPASS) {
        free(copy);
        xSemaphoreGive(ota_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t station_start_network(void)
{
    ota_lock = xSemaphoreCreateMutex();
    if (!ota_lock)
        return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "HoloWeather-%02X%02X", mac[4], mac[5]);
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "Weather%02X%02X", mac[4], mac[5]);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 2;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    wifi_config_t sta = {0};
    configured = load_wifi(&sta);
    if (configured)
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "time.cloudflare.com", "pool.ntp.org"));
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp));
    httpd_config_t server = HTTPD_DEFAULT_CONFIG();
    server.stack_size = 8192;
    server.max_uri_handlers = 8;
    server.lru_purge_enable = true;
    httpd_handle_t handle;
    esp_err_t e = httpd_start(&handle, &server);
    if (e != ESP_OK)
        return e;
    httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = home},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status},
        {.uri = "/api/config", .method = HTTP_POST, .handler = configure},
        {.uri = "/api/frame", .method = HTTP_GET, .handler = image_frame},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = upload_ota},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
        ESP_ERROR_CHECK(httpd_register_uri_handler(handle, &routes[i]));
    ESP_LOGI("NET", "AP %s password %s; setup http://192.168.4.1/", ap.ap.ssid, ap.ap.password);
    return ESP_OK;
}
