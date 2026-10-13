#include "station.h"
#include "cJSON.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static SemaphoreHandle_t lock;
static const char *paths[] = {"/spiffs/boot.jpg", "/spiffs/clock.jpg", "/spiffs/charge.jpg"};
static char *encoded;
static size_t received;
static int expected, next_packet, kind;

bool station_jpeg_dimensions(const uint8_t *data, size_t n, int *width, int *height)
{
    if (n < 4 || data[0] != 0xff || data[1] != 0xd8)
        return false;
    for (size_t i = 2; i + 4 <= n;) {
        if (data[i++] != 0xff)
            return false;
        while (i < n && data[i] == 0xff)
            i++;
        if (i >= n)
            return false;
        uint8_t marker = data[i++];
        if (marker == 0xd9 || marker == 0xda)
            return false;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        if (i + 2 > n)
            return false;
        size_t size = ((size_t)data[i] << 8) | data[i + 1];
        if (size < 2 || size > n - i)
            return false;
        if (marker == 0xc0) {
            if (size < 8 || data[i + 2] != 8)
                return false;
            *height = ((int)data[i + 3] << 8) | data[i + 4];
            *width = ((int)data[i + 5] << 8) | data[i + 6];
            return (*width == LCD_WIDTH && *height == LCD_HEIGHT) ||
                   (*width == LCD_HEIGHT && *height == LCD_WIDTH);
        }
        if (marker >= 0xc1 && marker <= 0xc3)
            return false;
        i += size;
    }
    return false;
}
static bool integer(const cJSON *obj, const char *key, int low, int high, int *out)
{
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble) || j->valuedouble < low ||
        j->valuedouble > high || j->valuedouble != (int)j->valuedouble)
        return false;
    *out = j->valuedouble;
    return true;
}
static void clear_transfer(void)
{
    free(encoded);
    encoded = NULL;
    received = 0;
    expected = 0;
    next_packet = 1;
}
esp_err_t station_images_init(void)
{
    lock = xSemaphoreCreateMutex();
    if (!lock)
        return ESP_ERR_NO_MEM;
    esp_vfs_spiffs_conf_t cfg = {.base_path = "/spiffs",
                                 .partition_label = "spiffs",
                                 .max_files = 4,
                                 .format_if_mount_failed = true};
    return esp_vfs_spiffs_register(&cfg);
}
esp_err_t station_image_packet(const void *object)
{
    const cJSON *o = object;
    int number, total, image_kind;
    const cJSON *chunk = cJSON_GetObjectItemCaseSensitive(o, "value");
    if (!chunk)
        chunk = cJSON_GetObjectItemCaseSensitive(o, "data");
    if (!lock || !cJSON_IsString(chunk) || !integer(o, "number", 1, 128, &number) ||
        !integer(o, "totalPackets", 1, 128, &total) || number > total)
        return ESP_ERR_INVALID_ARG;
    if (!integer(o, "type", 0, 2, &image_kind) && !integer(o, "kind", 0, 2, &image_kind))
        return ESP_ERR_INVALID_ARG;
    size_t length = strlen(chunk->valuestring);
    if (!length || length > 1024)
        return ESP_ERR_INVALID_SIZE;
    xSemaphoreTake(lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (number == 1) {
        clear_transfer();
        encoded = calloc(16385, 1);
        if (!encoded) {
            err = ESP_ERR_NO_MEM;
            goto done;
        }
        expected = total;
        kind = image_kind;
    }
    if (!encoded || expected != total || kind != image_kind || next_packet != number ||
        length > 16384 - received) {
        err = ESP_ERR_INVALID_ARG;
        clear_transfer();
        goto done;
    }
    memcpy(encoded + received, chunk->valuestring, length);
    received += length;
    next_packet++;
    if (number == total) {
        size_t decoded_length = 0;
        int rc = mbedtls_base64_decode(NULL, 0, &decoded_length, (uint8_t *)encoded, received);
        if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || decoded_length < 4 ||
            decoded_length > 12288) {
            err = ESP_ERR_INVALID_SIZE;
            clear_transfer();
            goto done;
        }
        uint8_t *jpeg = malloc(decoded_length);
        if (!jpeg) {
            err = ESP_ERR_NO_MEM;
            clear_transfer();
            goto done;
        }
        rc = mbedtls_base64_decode(jpeg, decoded_length, &decoded_length, (uint8_t *)encoded,
                                   received);
        int w, h;
        if (rc || !station_jpeg_dimensions(jpeg, decoded_length, &w, &h)) {
            free(jpeg);
            err = ESP_ERR_INVALID_ARG;
            clear_transfer();
            goto done;
        }
        FILE *f = fopen("/spiffs/upload.tmp", "wb");
        bool saved = false;
        if (f) {
            saved = fwrite(jpeg, 1, decoded_length, f) == decoded_length;
            int closed = fclose(f);
            saved = saved && closed == 0;
        }
        free(jpeg);
        if (saved) {
            remove(paths[kind]);
            if (rename("/spiffs/upload.tmp", paths[kind]))
                err = ESP_FAIL;
            else
                station_status("Background saved");
        } else {
            remove("/spiffs/upload.tmp");
            err = ESP_ERR_NO_MEM;
        }
        clear_transfer();
    }
done:
    xSemaphoreGive(lock);
    return err;
}
bool station_image_read(int kind, uint8_t **data, size_t *length)
{
    if (!lock || kind < 0 || kind > 2)
        return false;
    *data = NULL;
    *length = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    FILE *f = fopen(paths[kind], "rb");
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0) {
            long n = ftell(f);
            if (n > 0 && n <= 12288) {
                rewind(f);
                uint8_t *buffer = malloc(n);
                if (buffer && fread(buffer, 1, n, f) == (size_t)n) {
                    *data = buffer;
                    *length = n;
                } else
                    free(buffer);
            }
        }
        fclose(f);
    }
    xSemaphoreGive(lock);
    return *data != NULL;
}
