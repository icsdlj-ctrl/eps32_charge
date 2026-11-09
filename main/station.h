#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#define STATION_VERSION "1.1.6-weather.6"
#define LCD_WIDTH 284
#define LCD_HEIGHT 76
#define LCD_MOSI 1
#define LCD_SCLK 0
#define LCD_CS 6
#define LCD_DC 10
#define LCD_RST 3
#define LCD_BACKLIGHT 7
#define I2C_SDA 4
#define I2C_SCL 5
#define STATION_PORTS 4

typedef enum {
    PAGE_AUTO = 0,
    PAGE_CHARGE = 1,
    PAGE_CLOCK = 2,
    PAGE_WEATHER = 3,
    PAGE_BOOT = 4
} station_page_t;
typedef struct {
    uint8_t brightness, night_brightness, night_enabled, skin;
    uint16_t max_power, night_start, night_stop, timeout;
    uint8_t page;
} station_config_t;
typedef struct {
    float voltage, current, power;
    double charge_mah, energy_wh;
    uint32_t seconds;
    bool valid, connected;
    uint8_t address;
} station_port_t;
typedef struct {
    station_config_t config;
    station_port_t ports[STATION_PORTS];
    bool wifi_connected;
    char ip[16];
    char wifi_ssid[33];
    int wifi_reason;
    uint8_t ota_progress;
    bool ota_busy;
    char status[64];
    uint8_t alert_mode;
    char alert_source[12];
} station_snapshot_t;
typedef struct {
    char name[32];
    char addr[18];
    int8_t rssi;
} station_ble_device_t;

void station_init_state(void);
void station_snapshot(station_snapshot_t *out);
void station_update_port(int index, const station_port_t *port);
void station_network_state(bool connected, const char *ip);
void station_wifi_diag(const char *ssid, int reason);
const char *station_wifi_fail(int reason);
void station_status(const char *text);
void station_ota_state(bool busy, uint8_t progress, const char *text);
esp_err_t station_load_settings(void);
esp_err_t station_apply_json(const char *text, size_t length);
size_t station_json(char *out, size_t size);
bool station_night_window(unsigned now, unsigned start, unsigned stop);
esp_err_t station_start_power(void);
esp_err_t station_start_display(void);
bool station_copy_frame(uint8_t *buffer, size_t offset, size_t size);
char *station_status_json(void);
esp_err_t station_start_usb(void);
bool station_catch_boot_stop(void);
bool station_app_held(void);
void station_set_held(bool held);
void station_render(uint16_t *buffer, const station_snapshot_t *s, int page, const struct tm *local,
                    bool valid, unsigned anim);
esp_err_t station_present_write(size_t offset, const uint8_t *data, size_t length, bool flush);
esp_err_t station_present_flush(void);
void station_present_clear(void);
bool station_present_active(void);
bool station_rotate_portrait(uint16_t *buffer);
esp_err_t station_start_network(void);
esp_err_t station_save_wifi(const char *ssid, const char *password);
esp_err_t station_scan_wifi(void);
int station_signal_percent(int rssi);
esp_err_t station_start_ble(void);
esp_err_t station_scan_ble(void);
int station_ble_copy(station_ble_device_t *out, int max);
esp_err_t station_start_ota(const char *url);
esp_err_t station_images_init(void);
esp_err_t station_image_packet(const void *json_object);
bool station_image_read(int kind, uint8_t **data, size_t *length);
