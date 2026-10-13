/* ESP-IDF ST7789 driver with command values recovered from the supplied image.
 * GUI is newly drawn; it is not the author's original LVGL source. */
#include "station.h"
#include "font.h"
#include "hs_weather.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "jpeg_decoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

static uint16_t *frame;
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t transfer_done;
static SemaphoreHandle_t drawing;
bool station_copy_frame(uint8_t *buffer, size_t offset, size_t n)
{
    if (!buffer || offset + n > LCD_WIDTH * LCD_HEIGHT * 2 || !drawing ||
        xSemaphoreTake(drawing, pdMS_TO_TICKS(1000)) != pdTRUE)
        return false;
    memcpy(buffer, (uint8_t *)frame + offset, n);
    xSemaphoreGive(drawing);
    return true;
}
extern bool station_jpeg_dimensions(const uint8_t *, size_t, int *, int *);
static bool IRAM_ATTR on_transfer(esp_lcd_panel_io_handle_t io,
                                  esp_lcd_panel_io_event_data_t *event, void *arg)
{
    (void)io;
    (void)event;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)arg, &wake);
    return wake == pdTRUE;
}
static void display_task(void *arg)
{
    (void)arg;
    unsigned last_brightness = UINT32_MAX;
    uint32_t ticks = 0;
    int64_t boot_end = esp_timer_get_time() + 2000000;
    for (;;) {
        station_snapshot_t s;
        station_snapshot(&s);
        time_t now = time(NULL);
        struct tm local = {0};
        bool valid = now > 1704067200;
        localtime_r(&now, &local);
        unsigned brightness = s.config.brightness;
        if (valid && s.config.night_enabled &&
            station_night_window(local.tm_hour * 100 + local.tm_min, s.config.night_start,
                                 s.config.night_stop))
            brightness = s.config.night_brightness;
        if (brightness != last_brightness) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, brightness * 8191 / 100);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            last_brightness = brightness;
        }
        int page = s.config.page;
        if (page == PAGE_AUTO) {
            bool load = false;
            for (int i = 0; i < 4; i++)
                load |= s.ports[i].connected;
            unsigned phase = (esp_timer_get_time() / 1000000) % 30;
            page = phase < 10   ? PAGE_CLOCK
                   : phase < 20 ? PAGE_WEATHER
                   : load       ? PAGE_CHARGE
                                : PAGE_CLOCK;
        }
        if (esp_timer_get_time() < boot_end)
            page = PAGE_BOOT;
        xSemaphoreTake(drawing, portMAX_DELAY);
        station_render(frame, &s, page, &local, valid);
        esp_err_t e = esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, frame);
        if (e == ESP_OK)
            xSemaphoreTake(transfer_done, portMAX_DELAY);
        else
            ESP_LOGW("LCD", "draw error %s", esp_err_to_name(e));
        xSemaphoreGive(drawing);
        if (++ticks % 25 == 0)
            ESP_LOGI("STATION", "page=%d wifi=%d INA=%d%d%d%d free=%lu", page, s.wifi_connected,
                     s.ports[0].valid, s.ports[1].valid, s.ports[2].valid, s.ports[3].valid,
                     (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}
esp_err_t station_start_display(void)
{
    frame = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    transfer_done = xSemaphoreCreateBinary();
    drawing = xSemaphoreCreateMutex();
    if (!frame || !transfer_done || !drawing)
        return ESP_ERR_NO_MEM;
    spi_bus_config_t bus = {.sclk_io_num = LCD_SCLK,
                            .mosi_io_num = LCD_MOSI,
                            .miso_io_num = -1,
                            .quadwp_io_num = -1,
                            .quadhd_io_num = -1,
                            .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2};
    esp_err_t e = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK)
        return e;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t ioc = {.dc_gpio_num = LCD_DC,
                                         .cs_gpio_num = LCD_CS,
                                         .pclk_hz = 40000000,
                                         .lcd_cmd_bits = 8,
                                         .lcd_param_bits = 8,
                                         .spi_mode = 0,
                                         .trans_queue_depth = 1,
                                         .on_color_trans_done = on_transfer,
                                         .user_ctx = transfer_done};
    e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioc, &io);
    if (e != ESP_OK)
        return e;
    esp_lcd_panel_dev_config_t pc = {.reset_gpio_num = LCD_RST,
                                     .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                     .bits_per_pixel = 16};
    e = esp_lcd_new_panel_st7789(io, &pc, &panel);
    if (e != ESP_OK)
        return e;
    e = esp_lcd_panel_reset(panel);
    if (e != ESP_OK)
        return e;
    e = esp_lcd_panel_init(panel);
    if (e != ESP_OK)
        return e;
    /* Original ST7789P3-specific porch, power and gamma sequence. */
    const uint8_t gamma0[] = {0xd0, 0x10, 0x21, 0x14, 0x15, 0x2d, 0x41,
                              0x44, 0x4f, 0x28, 0x0e, 0x0c, 0x1d, 0x1f};
    const uint8_t gamma1[] = {0xd0, 0x0f, 0x1b, 0x0d, 0x0d, 0x26, 0x42,
                              0x54, 0x50, 0x3e, 0x1a, 0x18, 0x22, 0x25};
#define CMD(command, ...)                                                                          \
    do {                                                                                           \
        const uint8_t values[] = {__VA_ARGS__};                                                    \
        e = esp_lcd_panel_io_tx_param(io, command, values, sizeof(values));                        \
        if (e != ESP_OK)                                                                           \
            return e;                                                                              \
    } while (0)
    CMD(0xb2, 0x0c, 0x0c, 0x00, 0x33, 0x33);
    CMD(0xb0, 0x00, 0xe0);
    CMD(0xb7, 0x45);
    CMD(0xbb, 0x1d);
    CMD(0xc0, 0x2c);
    CMD(0xc2, 0x01);
    CMD(0xc3, 0x19);
    CMD(0xc4, 0x20);
    CMD(0xc6, 0x0f);
    CMD(0xd0, 0xa4, 0xa1);
    CMD(0xd6, 0xa1);
    e = esp_lcd_panel_io_tx_param(io, 0xe0, gamma0, sizeof(gamma0));
    if (e != ESP_OK)
        return e;
    e = esp_lcd_panel_io_tx_param(io, 0xe1, gamma1, sizeof(gamma1));
    if (e != ESP_OK)
        return e;
    e = esp_lcd_panel_set_gap(panel, 18, 82);
    if (e != ESP_OK)
        return e;
    e = esp_lcd_panel_swap_xy(panel, true);
    if (e != ESP_OK)
        return e;
    e = esp_lcd_panel_mirror(panel, false, true);
    if (e != ESP_OK)
        return e;
    e = esp_lcd_panel_disp_on_off(panel, true);
    if (e != ESP_OK)
        return e;
    ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                 .timer_num = LEDC_TIMER_1,
                                 .duty_resolution = LEDC_TIMER_13_BIT,
                                 .freq_hz = 4000,
                                 .clk_cfg = LEDC_AUTO_CLK};
    e = ledc_timer_config(&timer);
    if (e != ESP_OK)
        return e;
    ledc_channel_config_t channel = {.gpio_num = LCD_BACKLIGHT,
                                     .speed_mode = LEDC_LOW_SPEED_MODE,
                                     .channel = LEDC_CHANNEL_0,
                                     .timer_sel = LEDC_TIMER_1,
                                     .duty = 0};
    e = ledc_channel_config(&channel);
    if (e != ESP_OK)
        return e;
    ESP_LOGI("LCD", "ST7789 284x76 SPI MOSI=1 SCLK=0 CS=6 DC=10 RST=3 BL=7");
    return xTaskCreate(display_task, "display", 6144, NULL, 5, NULL) == pdPASS ? ESP_OK
                                                                               : ESP_ERR_NO_MEM;
}
