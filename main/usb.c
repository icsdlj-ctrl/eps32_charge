/* Newline-delimited JSON over the existing USB Serial/JTAG port. No credentials
 * are echoed or included in replies. Logs and HS_REPLY messages share the port. */
#include "station.h"
#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
                esp_err_t e = query ? ESP_OK : station_apply_json(line, used);
                if (e == ESP_OK) {
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
    usb_serial_jtag_driver_config_t cfg = {.tx_buffer_size = 2048, .rx_buffer_size = 1024};
    esp_err_t e = usb_serial_jtag_driver_install(&cfg);
    if (e != ESP_OK)
        return e;
    return xTaskCreate(usb_task, "usb_config", 8192, NULL, 2, NULL) == pdPASS ? ESP_OK
                                                                              : ESP_ERR_NO_MEM;
}
