#include "device_storage.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "STORAGE";
static const char *partition = "storage";
static const char *legacy_namespaces[] = {"WiFi_cfg", "station", "devConfig", "hs_weather"};

static esp_err_t copy_key(nvs_handle_t src, nvs_handle_t dst, const nvs_entry_info_t *info)
{
    esp_err_t err = ESP_OK;
    switch (info->type) {
    case NVS_TYPE_U8: {
        uint8_t v;
        err = nvs_get_u8(src, info->key, &v);
        if (err == ESP_OK)
            err = nvs_set_u8(dst, info->key, v);
        break;
    }
    case NVS_TYPE_I8: {
        int8_t v;
        err = nvs_get_i8(src, info->key, &v);
        if (err == ESP_OK)
            err = nvs_set_i8(dst, info->key, v);
        break;
    }
    case NVS_TYPE_U16: {
        uint16_t v;
        err = nvs_get_u16(src, info->key, &v);
        if (err == ESP_OK)
            err = nvs_set_u16(dst, info->key, v);
        break;
    }
    case NVS_TYPE_I16: {
        int16_t v;
        err = nvs_get_i16(src, info->key, &v);
        if (err == ESP_OK)
            err = nvs_set_i16(dst, info->key, v);
        break;
    }
    case NVS_TYPE_U32: {
        uint32_t v;
        err = nvs_get_u32(src, info->key, &v);
        if (err == ESP_OK)
            err = nvs_set_u32(dst, info->key, v);
        break;
    }
    case NVS_TYPE_I32: {
        int32_t v;
        err = nvs_get_i32(src, info->key, &v);
        if (err == ESP_OK)
            err = nvs_set_i32(dst, info->key, v);
        break;
    }
    case NVS_TYPE_U64: {
        uint64_t v;
        err = nvs_get_u64(src, info->key, &v);
        if (err == ESP_OK)
            err = nvs_set_u64(dst, info->key, v);
        break;
    }
    case NVS_TYPE_I64: {
        int64_t v;
        err = nvs_get_i64(src, info->key, &v);
        if (err == ESP_OK)
            err = nvs_set_i64(dst, info->key, v);
        break;
    }
    case NVS_TYPE_STR: {
        size_t n = 0;
        err = nvs_get_str(src, info->key, NULL, &n);
        if (err != ESP_OK || n == 0 || n > 512)
            break;
        char *text = malloc(n);
        if (!text)
            return ESP_ERR_NO_MEM;
        err = nvs_get_str(src, info->key, text, &n);
        if (err == ESP_OK)
            err = nvs_set_str(dst, info->key, text);
        free(text);
        break;
    }
    case NVS_TYPE_BLOB: {
        size_t n = 0;
        err = nvs_get_blob(src, info->key, NULL, &n);
        if (err != ESP_OK || n == 0 || n > 1984)
            break;
        void *blob = malloc(n);
        if (!blob)
            return ESP_ERR_NO_MEM;
        err = nvs_get_blob(src, info->key, blob, &n);
        if (err == ESP_OK)
            err = nvs_set_blob(dst, info->key, blob, n);
        free(blob);
        break;
    }
    default:
        break;
    }
    return err;
}
static esp_err_t copy_namespace(const char *name)
{
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find("nvs", name, NVS_TYPE_ANY, &it);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (err != ESP_OK)
        return err;
    nvs_handle_t src, dst;
    err = nvs_open_from_partition("nvs", name, NVS_READONLY, &src);
    if (err != ESP_OK) {
        nvs_release_iterator(it);
        return err;
    }
    err = nvs_open_from_partition(partition, name, NVS_READWRITE, &dst);
    if (err != ESP_OK) {
        nvs_close(src);
        nvs_release_iterator(it);
        return err;
    }
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        err = copy_key(src, dst, &info);
        if (err == ESP_OK)
            err = nvs_entry_next(&it);
    }
    if (err == ESP_ERR_NVS_NOT_FOUND)
        err = nvs_commit(dst);
    nvs_close(dst);
    nvs_close(src);
    nvs_release_iterator(it);
    return err;
}
static esp_err_t migrate_legacy(void)
{
    nvs_handle_t meta;
    esp_err_t err = nvs_open_from_partition(partition, "meta", NVS_READWRITE, &meta);
    if (err != ESP_OK)
        return err;
    uint8_t ready = 0;
    if (nvs_get_u8(meta, "ready", &ready) == ESP_OK && ready == 1) {
        nvs_close(meta);
        return ESP_OK;
    }
    for (unsigned i = 0; i < sizeof(legacy_namespaces) / sizeof(legacy_namespaces[0]); i++) {
        err = copy_namespace(legacy_namespaces[i]);
        if (err != ESP_OK)
            break;
    }
    if (err == ESP_OK)
        err = nvs_set_u8(meta, "ready", 1);
    if (err == ESP_OK)
        err = nvs_set_u16(meta, "layout", 1);
    if (err == ESP_OK)
        err = nvs_commit(meta);
    nvs_close(meta);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "copied Wi-Fi, settings and weather into storage");
    return err;
}
esp_err_t storage_init(void)
{
    esp_err_t err = nvs_flash_init_partition("storage");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "storage partition unavailable (%s); using nvs", esp_err_to_name(err));
        partition = "nvs";
        return ESP_OK;
    }
    err = migrate_legacy();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "legacy copy incomplete (%s); still reading nvs", esp_err_to_name(err));
        partition = "nvs";
    }
    return ESP_OK;
}
esp_err_t storage_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out)
{
    return nvs_open_from_partition(partition, name, mode, out);
}
