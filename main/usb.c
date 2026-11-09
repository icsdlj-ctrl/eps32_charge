/* Newline-delimited JSON over the existing USB Serial/JTAG port. No credentials
 * are echoed or included in replies. Logs and HS_REPLY messages share the port. */
#include "station.h"
#include "device_storage.h"
#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool app_held;

static bool usb_driver_ready(void)
{
    usb_serial_jtag_driver_config_t cfg = {.tx_buffer_size = 4096, .rx_buffer_size = 1024};
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    return err == ESP_OK || err == ESP_ERR_INVALID_STATE;
}
static bool load_hold(void)
{
    nvs_handle_t handle;
    if (storage_open("meta", NVS_READONLY, &handle) != ESP_OK)
        return false;
    uint8_t held = 0;
    esp_err_t err = nvs_get_u8(handle, "hold", &held);
    nvs_close(handle);
    return err == ESP_OK && held == 1;
}
static void save_hold(bool held)
{
    nvs_handle_t handle;
    if (storage_open("meta", NVS_READWRITE, &handle) != ESP_OK)
        return;
    if (nvs_set_u8(handle, "hold", held ? 1 : 0) == ESP_OK)
        nvs_commit(handle);
    nvs_close(handle);
}
static bool seen_word(const uint8_t *data, size_t length, const char *word)
{
    size_t n = strlen(word);
    if (length < n)
        return false;
    for (size_t i = 0; i + n <= length; i++) {
        if (!memcmp(data + i, word, n))
            return true;
    }
    return false;
}
bool station_app_held(void)
{
    return app_held;
}
bool station_catch_boot_stop(void)
{
    if (load_hold()) {
        app_held = true;
        printf("\nAPP_HELD\n");
        fflush(stdout);
        return true;
    }
    if (!usb_driver_ready())
        return false;
    printf("\nBOOT_WAIT\n");
    fflush(stdout);
    uint8_t window[32] = {0};
    size_t used = 0;
    int64_t deadline = esp_timer_get_time() + 1000000;
    while (esp_timer_get_time() < deadline) {
        uint8_t chunk[32];
        int n = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), pdMS_TO_TICKS(40));
        for (int i = 0; i < n; i++) {
            if (used == sizeof(window)) {
                memmove(window, window + 1, sizeof(window) - 1);
                used--;
            }
            window[used++] = chunk[i];
        }
        if (seen_word(window, used, "STOP")) {
            save_hold(true);
            app_held = true;
            printf("\nAPP_HELD\n");
            fflush(stdout);
            return true;
        }
    }
    return false;
}
void station_set_held(bool held)
{
    save_hold(held);
    app_held = held;
    printf("\n%s\n", held ? "APP_HELD" : "APP_RUN");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

static void usb_task(void *arg)
{
    (void)arg;
    char line[768];
    size_t used = 0;
    bool overflow = false;
    for (;;) {
        uint8_t bytes[128];
        int n = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), pdMS_TO_TICKS(200));
        for (int i = 0; i < n; i++) {
            if (bytes[i] == '\r')
                continue;
            if (bytes[i] != '\n') {
                if (used < sizeof(line) - 1)
                    line[used++] = bytes[i];
                else
                    overflow = true;
                continue;
            }
            line[used] = 0;
            if (!overflow && used) {
                cJSON *command = cJSON_Parse(line);
                bool query = command && cJSON_HasObjectItem(command, "getStatus");
                cJSON_Delete(command);
                bool frame_push = strstr(line, "\"present\"") != NULL;
                esp_err_t e = query ? ESP_OK : station_apply_json(line, used);
                if (e == ESP_OK && frame_push) {
                    printf("\nHS_REPLY:{\"ok\":true}\n");
                } else if (e == ESP_OK) {
                    char *body = station_status_json();
                    if (body) {
                        printf("\nHS_REPLY:%s\n", body);
                        free(body);
                    } else
                        printf("\nHS_REPLY:{\"error\":\"out of memory\"}\n");
                } else
                    printf("\nHS_REPLY:{\"error\":\"%s\"}\n", esp_err_to_name(e));
                fflush(stdout);
            }
            used = 0;
            overflow = false;
            memset(line, 0, sizeof(line));
        }
    }
}
esp_err_t station_start_usb(void)
{
    if (!usb_driver_ready())
        return ESP_FAIL;
    return xTaskCreate(usb_task, "usb_config", 8192, NULL, 2, NULL) == pdPASS ? ESP_OK
                                                                              : ESP_ERR_NO_MEM;
}
