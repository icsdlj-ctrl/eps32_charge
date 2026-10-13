/* INA226 protocol follows TI's datasheet; board addresses/shunt and maximum
 * currents were recovered from the supplied holoStation 1.1.6 image. */
#include "station.h"
#include "charge_session.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static i2c_master_dev_handle_t devices[4];
static i2c_master_bus_handle_t bus;
static const uint8_t addresses[] = {0x43, 0x42, 0x41, 0x40}; /* A1,C1,C2,C3 */
static const float max_current[] = {3.5f, 3.5f, 7.0f, 6.0f};
static float lsb[4];
static esp_err_t write_reg(int i, uint8_t reg, uint16_t value)
{
    uint8_t b[] = {reg, value >> 8, value};
    return i2c_master_transmit(devices[i], b, 3, 100);
}
static esp_err_t read_reg(int i, uint8_t reg, uint16_t *value)
{
    uint8_t b[2];
    esp_err_t e = i2c_master_transmit_receive(devices[i], &reg, 1, b, 2, 100);
    if (e == ESP_OK)
        *value = ((uint16_t)b[0] << 8) | b[1];
    return e;
}
static void sampling(void *arg)
{
    (void)arg;
    charge_session_t sessions[4] = {0};
    bool calibrated[4] = {0};
    for (;;) {
        station_snapshot_t snapshot;
        station_snapshot(&snapshot);
        int64_t now = esp_timer_get_time();
        for (int i = 0; i < 4; i++) {
            station_port_t p = {.address = addresses[i]};
            if (!calibrated[i]) {
                /* Probe missing or unpowered channels without a stream of NACK errors. */
                if (i2c_master_probe(bus, addresses[i], 20) != ESP_OK) {
                    goto publish;
                }
                uint16_t manufacturer, device;
                if (read_reg(i, 0xfe, &manufacturer) != ESP_OK ||
                    read_reg(i, 0xff, &device) != ESP_OK || manufacturer != 0x5449 ||
                    (device & 0xfff0) != 0x2260) {
                    goto publish;
                }
                lsb[i] = max_current[i] / 32768.0f;
                uint16_t cal = (uint16_t)(0.00512f / (lsb[i] * 0.005f));
                if (write_reg(i, 5, cal) != ESP_OK || write_reg(i, 0, 0x4527) != ESP_OK) {
                    goto publish;
                }
                calibrated[i] = true;
                ESP_LOGI("INA226", "channel %d address 0x%02x cal %u", i, addresses[i], cal);
            }
            uint16_t voltage, current, power;
            if (read_reg(i, 2, &voltage) == ESP_OK && read_reg(i, 4, &current) == ESP_OK &&
                read_reg(i, 3, &power) == ESP_OK) {
                p.voltage = voltage * 0.00125f;
                p.current = (int16_t)current * lsb[i];
                p.power = power * 25.0f * lsb[i];
                p.valid = isfinite(p.voltage) && isfinite(p.current) && isfinite(p.power) &&
                          p.voltage <= 40 && fabsf(p.current) <= max_current[i] * 1.1f;
            } else
                calibrated[i] = false;
        publish:
            charge_session_update(&sessions[i], p.valid, p.current, p.power, now,
                                  snapshot.config.timeout);
            p.connected = p.valid && p.current >= CHARGE_CURRENT_THRESHOLD_A;
            p.charge_mah = sessions[i].charge_mah;
            p.energy_wh = sessions[i].energy_wh;
            p.seconds = sessions[i].seconds;
            station_update_port(i, &p);
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
esp_err_t station_start_power(void)
{
    i2c_master_bus_config_t cfg = {.i2c_port = I2C_NUM_0,
                                   .sda_io_num = I2C_SDA,
                                   .scl_io_num = I2C_SCL,
                                   .clk_source = I2C_CLK_SRC_DEFAULT,
                                   .glitch_ignore_cnt = 7,
                                   .flags.enable_internal_pullup = true};
    esp_err_t e = i2c_new_master_bus(&cfg, &bus);
    if (e != ESP_OK)
        return e;
    for (int i = 0; i < 4; i++) {
        i2c_device_config_t dev = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                   .device_address = addresses[i],
                                   .scl_speed_hz = 100000};
        e = i2c_master_bus_add_device(bus, &dev, &devices[i]);
        if (e != ESP_OK)
            return e;
    }
    return xTaskCreate(sampling, "power", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
