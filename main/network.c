#include "station.h"
#include "device_storage.h"
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

#define WIFI_MEMORY 5
#define SCAN_MAX 20
#define SCAN_KEEP 12
#define WIFI_BLOB_MAGIC 0x31494657u

typedef struct {
    char ssid[33];
    char password[64];
} wifi_slot_t;
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t open;
} scan_item_t;
typedef struct {
    uint32_t magic;
    uint8_t count;
    uint8_t reserved[3];
    wifi_slot_t slots[WIFI_MEMORY];
} wifi_blob_t;

static SemaphoreHandle_t ota_lock;
static SemaphoreHandle_t wifi_lock;
static SemaphoreHandle_t scan_sem;
static volatile bool configured;
static wifi_slot_t slots[WIFI_MEMORY];
static int slot_count;
static scan_item_t scan_items[SCAN_KEEP];
static int scan_count;
static bool scan_busy;
static bool scan_for_reconnect;
static int wifi_epoch;
static int scan_epoch;
static char avoid[33];
static TickType_t next_retry;
static const wifi_scan_config_t scan_cfg = {
    .show_hidden = false,
    .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    .scan_time.active.min = 80,
    .scan_time.active.max = 150,
};
static esp_err_t json_response(httpd_req_t *req, const char *body)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
}
static bool auth_failure(int reason)
{
    return reason == WIFI_REASON_AUTH_EXPIRE || reason == WIFI_REASON_AUTH_FAIL ||
           reason == WIFI_REASON_ASSOC_FAIL || reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_CONNECTION_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT;
}
static int cmp_rssi(const void *a, const void *b)
{
    return ((const scan_item_t *)b)->rssi - ((const scan_item_t *)a)->rssi;
}
static void cache_scan(const wifi_ap_record_t *recs, int n)
{
    static scan_item_t all[SCAN_MAX];
    int count = 0;
    for (int i = 0; i < n; i++) {
        char name[33] = {0};
        memcpy(name, recs[i].ssid, 32);
        if (!name[0])
            continue;
        int at = -1;
        for (int j = 0; j < count; j++) {
            if (!strcmp(all[j].ssid, name)) {
                at = j;
                break;
            }
        }
        if (at >= 0) {
            if (recs[i].rssi > all[at].rssi)
                all[at].rssi = recs[i].rssi;
            continue;
        }
        if (count >= SCAN_MAX)
            continue;
        memset(&all[count], 0, sizeof(all[count]));
        memcpy(all[count].ssid, name, sizeof(name));
        all[count].rssi = recs[i].rssi;
        all[count].open = recs[i].authmode == WIFI_AUTH_OPEN;
        count++;
    }
    qsort(all, count, sizeof(all[0]), cmp_rssi);
    if (count > SCAN_KEEP)
        count = SCAN_KEEP;
    memcpy(scan_items, all, count * sizeof(scan_items[0]));
    scan_count = count;
}
static int find_slot(const char *ssid)
{
    for (int i = 0; i < slot_count; i++) {
        if (!strcmp(slots[i].ssid, ssid))
            return i;
    }
    return -1;
}
static bool scanned_open(const char *ssid)
{
    for (int i = 0; i < scan_count; i++) {
        if (scan_items[i].open && !strcmp(scan_items[i].ssid, ssid))
            return true;
    }
    return false;
}
static int best_slot(void)
{
    int best = -1;
    int8_t best_rssi = -128;
    for (int i = 0; i < scan_count; i++) {
        int slot = find_slot(scan_items[i].ssid);
        if (slot < 0)
            continue;
        if (avoid[0] && slot_count > 1 && !strcmp(scan_items[i].ssid, avoid))
            continue;
        if (scan_items[i].rssi > best_rssi) {
            best_rssi = scan_items[i].rssi;
            best = slot;
        }
    }
    return best;
}
static void remember_slot(const char *ssid, const char *password)
{
    int found = find_slot(ssid);
    if (found >= 0) {
        memmove(&slots[found], &slots[found + 1], (slot_count - found - 1) * sizeof(slots[0]));
        slot_count--;
    }
    int keep = slot_count < WIFI_MEMORY ? slot_count : WIFI_MEMORY - 1;
    memmove(&slots[1], &slots[0], keep * sizeof(slots[0]));
    memset(&slots[0], 0, sizeof(slots[0]));
    memcpy(slots[0].ssid, ssid, strlen(ssid));
    memcpy(slots[0].password, password, strlen(password));
    slot_count = keep + 1;
}
static esp_err_t store_slots(void)
{
    nvs_handle_t h;
    esp_err_t e = storage_open("WiFi_cfg", NVS_READWRITE, &h);
    if (e != ESP_OK)
        return e;
    e = nvs_set_u8(h, "n", (uint8_t)slot_count);
    for (int i = 0; e == ESP_OK && i < slot_count; i++) {
        const char *ssid_keys[] = {"s0", "s1", "s2", "s3", "s4"};
        const char *password_keys[] = {"p0", "p1", "p2", "p3", "p4"};
        e = nvs_set_str(h, ssid_keys[i], slots[i].ssid);
        if (e == ESP_OK)
            e = nvs_set_str(h, password_keys[i], slots[i].password);
    }
    if (e == ESP_OK && slot_count > 0) {
        e = nvs_set_str(h, "wifi_ssid", slots[0].ssid);
        if (e == ESP_OK)
            e = nvs_set_str(h, "wifi_passwd", slots[0].password);
        if (e == ESP_OK)
            e = nvs_set_u8(h, "wifi_state", 1);
    }
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    return e;
}
static esp_err_t apply_slot(const wifi_slot_t *slot)
{
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, slot->ssid, strlen(slot->ssid));
    memcpy(config.sta.password, slot->password, strlen(slot->password));
    config.sta.pmf_cfg.capable = true;
    if (!slot->password[0])
        config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (e == ESP_OK) {
        configured = true;
        station_wifi_diag(slot->ssid, 0);
        e = esp_wifi_connect();
    }
    return e;
}
static void start_reconnect_scan(void)
{
    xSemaphoreTake(wifi_lock, portMAX_DELAY);
    if (scan_busy || slot_count <= 1) {
        bool single = slot_count <= 1;
        xSemaphoreGive(wifi_lock);
        if (single)
            esp_wifi_connect();
        return;
    }
    scan_for_reconnect = true;
    scan_epoch = wifi_epoch;
    scan_busy = true;
    xSemaphoreGive(wifi_lock);
    if (esp_wifi_scan_start(&scan_cfg, false) != ESP_OK) {
        xSemaphoreTake(wifi_lock, portMAX_DELAY);
        scan_busy = false;
        scan_for_reconnect = false;
        xSemaphoreGive(wifi_lock);
        esp_wifi_connect();
    }
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (configured)
            start_reconnect_scan();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        static wifi_ap_record_t recs[SCAN_MAX];
        uint16_t n = SCAN_MAX;
        esp_wifi_scan_get_ap_records(&n, recs);
        station_snapshot_t snap;
        station_snapshot(&snap);
        xSemaphoreTake(wifi_lock, portMAX_DELAY);
        cache_scan(recs, n);
        bool reconnect = scan_epoch == wifi_epoch && (scan_for_reconnect || !snap.wifi_connected);
        int slot = reconnect ? best_slot() : -1;
        wifi_slot_t chosen = {0};
        if (slot >= 0)
            chosen = slots[slot];
        scan_for_reconnect = false;
        scan_busy = false;
        xSemaphoreGive(wifi_lock);
        xSemaphoreGive(scan_sem);
        if (reconnect) {
            if (slot >= 0)
                apply_slot(&chosen);
            else
                esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        station_network_state(false, NULL);
        wifi_event_sta_disconnected_t *event = data;
        station_wifi_diag(NULL, event->reason);
        ESP_LOGW("NET", "Wi-Fi disconnected reason %u: %s", event->reason,
                 station_wifi_fail(event->reason));
        if (configured) {
            if (auth_failure(event->reason)) {
                wifi_config_t cur = {0};
                if (esp_wifi_get_config(WIFI_IF_STA, &cur) == ESP_OK) {
                    xSemaphoreTake(wifi_lock, portMAX_DELAY);
                    memset(avoid, 0, sizeof(avoid));
                    memcpy(avoid, cur.sta.ssid, 32);
                    xSemaphoreGive(wifi_lock);
                }
            }
            if (xTaskGetTickCount() < next_retry)
                esp_wifi_connect();
            else {
                next_retry = xTaskGetTickCount() + pdMS_TO_TICKS(6000);
                start_reconnect_scan();
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        station_network_state(true, ip);
        station_status("Connected");
        xSemaphoreTake(wifi_lock, portMAX_DELAY);
        avoid[0] = 0;
        xSemaphoreGive(wifi_lock);
        ESP_LOGI("NET", "station IP %s", ip);
        hs_weather_relocate();
    }
}
esp_err_t station_save_wifi(const char *ssid, const char *password)
{
    if (!ssid || !password || strlen(ssid) < 1 || strlen(ssid) > 32 || strlen(password) > 63 ||
        (strlen(password) > 0 && strlen(password) < 8))
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(wifi_lock, portMAX_DELAY);
    char kept[64] = {0};
    if (!password[0]) {
        int slot = find_slot(ssid);
        if (slot >= 0)
            memcpy(kept, slots[slot].password, sizeof(kept));
        else if (scanned_open(ssid))
            kept[0] = 0;
        else {
            xSemaphoreGive(wifi_lock);
            return ESP_ERR_NOT_FOUND;
        }
        if (kept[0] && strlen(kept) < 8) {
            xSemaphoreGive(wifi_lock);
            return ESP_ERR_NOT_FOUND;
        }
        password = kept;
    }
    remember_slot(ssid, password);
    wifi_epoch++;
    scan_for_reconnect = false;
    avoid[0] = 0;
    wifi_slot_t chosen = slots[0];
    xSemaphoreGive(wifi_lock);
    esp_err_t e = store_slots();
    if (e != ESP_OK)
        return e;
    configured = false;
    esp_wifi_disconnect();
    /* Let the disconnect event run before enabling retries for the new SSID. */
    vTaskDelay(pdMS_TO_TICKS(50));
    return apply_slot(&chosen);
}
esp_err_t station_scan_wifi(void)
{
    xSemaphoreTake(wifi_lock, portMAX_DELAY);
    bool busy = scan_busy;
    if (!busy) {
        scan_for_reconnect = false;
        scan_busy = true;
    }
    xSemaphoreGive(wifi_lock);
    if (!busy) {
        xSemaphoreTake(scan_sem, 0);
        esp_err_t e = esp_wifi_scan_start(&scan_cfg, false);
        if (e != ESP_OK) {
            xSemaphoreTake(wifi_lock, portMAX_DELAY);
            scan_busy = false;
            xSemaphoreGive(wifi_lock);
            return e;
        }
    }
    return xSemaphoreTake(scan_sem, pdMS_TO_TICKS(8000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
static bool read_named_slots(nvs_handle_t h)
{
    uint8_t n = 0;
    if (nvs_get_u8(h, "n", &n) != ESP_OK || n == 0)
        return false;
    if (n > WIFI_MEMORY)
        n = WIFI_MEMORY;
    for (int i = 0; i < n; i++) {
        const char *ssid_keys[] = {"s0", "s1", "s2", "s3", "s4"};
        const char *password_keys[] = {"p0", "p1", "p2", "p3", "p4"};
        char ssid[33] = {0}, password[64] = {0};
        size_t ns = sizeof(ssid), ms = sizeof(password);
        if (nvs_get_str(h, ssid_keys[i], ssid, &ns) != ESP_OK || !ssid[0] || strlen(ssid) > 32)
            return false;
        if (nvs_get_str(h, password_keys[i], password, &ms) != ESP_OK || strlen(password) > 63)
            password[0] = 0;
        memset(&slots[i], 0, sizeof(slots[i]));
        memcpy(slots[i].ssid, ssid, strlen(ssid));
        memcpy(slots[i].password, password, strlen(password));
    }
    slot_count = n;
    return true;
}
static bool read_blob_slots(nvs_handle_t h)
{
    size_t n = 0;
    if (nvs_get_blob(h, "wifi_mem", NULL, &n) != ESP_OK || n < 8 || n > 1024)
        return false;
    uint8_t *raw = malloc(n);
    if (!raw)
        return false;
    bool ok = false;
    if (nvs_get_blob(h, "wifi_mem", raw, &n) == ESP_OK) {
        uint32_t magic = 0;
        memcpy(&magic, raw, sizeof(magic));
        uint8_t count = raw[4];
        int available = (int)((n - 8) / sizeof(wifi_slot_t));
        if (magic == WIFI_BLOB_MAGIC && count > 0 && count <= available && count <= WIFI_MEMORY) {
            memcpy(slots, raw + 8, count * sizeof(wifi_slot_t));
            slot_count = count;
            for (int i = 0; i < slot_count; i++) {
                slots[i].ssid[32] = 0;
                slots[i].password[63] = 0;
            }
            ok = true;
        }
    }
    free(raw);
    return ok;
}
static bool read_legacy_slot(nvs_handle_t h, const char *ssid_key, const char *password_key)
{
    char ssid[33] = {0}, password[64] = {0};
    size_t ns = sizeof(ssid), ms = sizeof(password);
    esp_err_t e = nvs_get_str(h, ssid_key, ssid, &ns);
    if (e == ESP_OK)
        e = nvs_get_str(h, password_key, password, &ms);
    if (e != ESP_OK || !ssid[0] || strlen(ssid) > 32 || strlen(password) > 63)
        return false;
    memset(&slots[0], 0, sizeof(slots[0]));
    memcpy(slots[0].ssid, ssid, strlen(ssid));
    memcpy(slots[0].password, password, strlen(password));
    slot_count = 1;
    return true;
}
static bool load_slots(void)
{
    slot_count = 0;
    nvs_handle_t h;
    if (storage_open("WiFi_cfg", NVS_READONLY, &h) == ESP_OK) {
        bool found = read_named_slots(h) || read_blob_slots(h) || read_legacy_slot(h, "wifi_ssid", "wifi_passwd");
        nvs_close(h);
        if (found)
            return true;
    }
    if (storage_open("station", NVS_READONLY, &h) == ESP_OK) {
        bool found = read_legacy_slot(h, "ssid", "password");
        nvs_close(h);
        if (found)
            return true;
    }
    return false;
}
static bool load_wifi(wifi_config_t *cfg)
{
    if (!load_slots())
        return false;
    memcpy(cfg->sta.ssid, slots[0].ssid, strlen(slots[0].ssid));
    memcpy(cfg->sta.password, slots[0].password, strlen(slots[0].password));
    cfg->sta.pmf_cfg.capable = true;
    if (!slots[0].password[0])
        cfg->sta.threshold.authmode = WIFI_AUTH_OPEN;
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
    cJSON_AddStringToObject(root, "ssid", s.wifi_ssid);
    cJSON_AddNumberToObject(root, "wifiReason", s.wifi_reason);
    if (!s.wifi_connected && s.wifi_reason)
        cJSON_AddStringToObject(root, "wifiFail", station_wifi_fail(s.wifi_reason));
    cJSON_AddStringToObject(root, "status", s.status);
    cJSON_AddBoolToObject(root, "appHeld", station_app_held());
    wifi_config_t ap_now = {0};
    if (esp_wifi_get_config(WIFI_IF_AP, &ap_now) == ESP_OK) {
        cJSON_AddStringToObject(root, "apSsid", (const char *)ap_now.ap.ssid);
        cJSON_AddStringToObject(root, "apPassword", (const char *)ap_now.ap.password);
    }
    cJSON *saved = cJSON_AddArrayToObject(root, "wifiSaved");
    cJSON *found = cJSON_AddArrayToObject(root, "wifiScan");
    if (wifi_lock && xSemaphoreTake(wifi_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
        for (int i = 0; i < slot_count; i++)
            cJSON_AddItemToArray(saved, cJSON_CreateString(slots[i].ssid));
        for (int i = 0; i < scan_count; i++) {
            cJSON *ap = cJSON_CreateObject();
            cJSON_AddStringToObject(ap, "ssid", scan_items[i].ssid);
            cJSON_AddNumberToObject(ap, "rssi", scan_items[i].rssi);
            cJSON_AddNumberToObject(ap, "signal", station_signal_percent(scan_items[i].rssi));
            cJSON_AddBoolToObject(ap, "open", scan_items[i].open);
            cJSON_AddItemToArray(found, ap);
        }
        xSemaphoreGive(wifi_lock);
    }
    wifi_ap_record_t current = {0};
    if (esp_wifi_sta_get_ap_info(&current) == ESP_OK) {
        cJSON_AddNumberToObject(root, "wifiRssi", current.rssi);
        cJSON_AddNumberToObject(root, "wifiSignal", station_signal_percent(current.rssi));
    }
    cJSON *ble = cJSON_AddArrayToObject(root, "bleScan");
    station_ble_device_t devices[12];
    int ble_count = station_ble_copy(devices, 12);
    for (int i = 0; i < ble_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", devices[i].name);
        cJSON_AddStringToObject(item, "addr", devices[i].addr);
        cJSON_AddNumberToObject(item, "rssi", devices[i].rssi);
        cJSON_AddNumberToObject(item, "signal", station_signal_percent(devices[i].rssi));
        cJSON_AddItemToArray(ble, item);
    }
    cJSON_AddBoolToObject(root, "otaBusy", s.ota_busy);
    cJSON_AddNumberToObject(root, "alertMode", s.alert_mode);
    cJSON_AddStringToObject(root, "alertSource", s.alert_source);
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
        cJSON_AddStringToObject(weather, "mode", w.config.auto_location ? "auto" : "custom");
        cJSON_AddStringToObject(weather, "language", "zh");
        cJSON_AddNumberToObject(weather, "latitude", w.config.latitude_e6 / 1000000.0);
        cJSON_AddNumberToObject(weather, "longitude", w.config.longitude_e6 / 1000000.0);
        if (w.last_error != ESP_OK)
            cJSON_AddStringToObject(weather, "error", esp_err_to_name(w.last_error));
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
static esp_err_t present_frame(httpd_req_t *req)
{
    const size_t total = (size_t)LCD_WIDTH * LCD_HEIGHT * 2;
    if ((size_t)req->content_len != total) {
        httpd_resp_set_status(req, "400 Bad Request");
        return json_response(req, "{\"error\":\"rgb565 length\"}");
    }
    uint8_t buf[1024];
    size_t got = 0;
    size_t held = 0;
    while (got < total) {
        size_t room = sizeof(buf) - held;
        size_t remain = total - got - held;
        if (room > remain)
            room = remain;
        int n = httpd_req_recv(req, (char *)buf + held, room);
        if (n <= 0)
            return httpd_resp_send_500(req);
        held += (size_t)n;
        size_t even = held & ~1u;
        if (!even)
            continue;
        esp_err_t err = station_present_write(got, buf, even, false);
        if (err != ESP_OK) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            return json_response(req, "{\"error\":\"present\"}");
        }
        got += even;
        if (held > even)
            buf[0] = buf[held - 1];
        held -= even;
    }
    if (station_present_flush() != ESP_OK)
        return httpd_resp_send_500(req);
    return json_response(req, "{\"ok\":true}");
}
static esp_err_t present_clear(httpd_req_t *req)
{
    station_present_clear();
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
    wifi_lock = xSemaphoreCreateMutex();
    scan_sem = xSemaphoreCreateBinary();
    if (!ota_lock || !wifi_lock || !scan_sem)
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
    if (configured) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        char name[33] = {0};
        memcpy(name, sta.sta.ssid, sizeof(sta.sta.ssid));
        station_wifi_diag(name, 0);
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "time.cloudflare.com", "pool.ntp.org"));
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp));
    httpd_config_t server = HTTPD_DEFAULT_CONFIG();
    server.stack_size = 8192;
    server.max_uri_handlers = 12;
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
        {.uri = "/api/present", .method = HTTP_POST, .handler = present_frame},
        {.uri = "/api/present/clear", .method = HTTP_POST, .handler = present_clear},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = upload_ota},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
        ESP_ERROR_CHECK(httpd_register_uri_handler(handle, &routes[i]));
    ESP_LOGI("NET", "AP %s password %s; setup http://192.168.4.1/", ap.ap.ssid, ap.ap.password);
    return ESP_OK;
}
