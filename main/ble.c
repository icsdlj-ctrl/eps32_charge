/* GATT service/characteristic IDs and JSON names recovered from holoStation.
 * NimBLE lifecycle follows Espressif's bleprph example (Apache-2.0). */
#include "station.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const ble_uuid16_t service_uuid = BLE_UUID16_INIT(0x180a);
static const ble_uuid16_t tx_uuid = BLE_UUID16_INIT(0x2a29);
static const ble_uuid16_t rx_uuid = BLE_UUID16_INIT(0x2a24);
static uint16_t tx_handle, connection = BLE_HS_CONN_HANDLE_NONE;
static bool subscribed;
static uint8_t address_type;
typedef struct {
    uint16_t length;
    char text[513];
} ble_command_t;
static QueueHandle_t commands;
static void advertise(void);

static int access_gatt(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char json[640];
        size_t n = station_json(json, sizeof(json));
        return n && os_mbuf_append(ctxt->om, json, n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        ble_command_t command = {0};
        uint16_t n = OS_MBUF_PKTLEN(ctxt->om);
        if (n < 1 || n > 512)
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (ble_hs_mbuf_to_flat(ctxt->om, command.text, 512, &command.length))
            return BLE_ATT_ERR_UNLIKELY;
        command.text[command.length] = 0;
        if (command.length >= 10 && !memcmp(command.text, "I'mIronMan", 10))
            return 0;
        return xQueueSend(commands, &command, 0) == pdTRUE ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &service_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){{.uuid = &tx_uuid.u,
                                      .access_cb = access_gatt,
                                      .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
                                      .val_handle = &tx_handle},
                                     {.uuid = &rx_uuid.u,
                                      .access_cb = access_gatt,
                                      .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
                                     {0}}},
    {0}};
static int on_gap(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            connection = event->connect.conn_handle;
            ESP_LOGI("BLE", "connected");
        } else
            advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        connection = BLE_HS_CONN_HANDLE_NONE;
        subscribed = false;
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == tx_handle)
            subscribed = event->subscribe.cur_notify;
        break;
    default:
        break;
    }
    return 0;
}
static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"HoloStationV3";
    fields.name_len = 13;
    fields.name_is_complete = 1;
    fields.uuids16 = (ble_uuid16_t[]){BLE_UUID16_INIT(0x180a)};
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc) {
        ESP_LOGE("BLE", "advertise fields %d", rc);
        return;
    }
    struct ble_gap_adv_params params = {.conn_mode = BLE_GAP_CONN_MODE_UND,
                                        .disc_mode = BLE_GAP_DISC_MODE_GEN};
    rc = ble_gap_adv_start(address_type, NULL, BLE_HS_FOREVER, &params, on_gap, NULL);
    if (rc)
        ESP_LOGE("BLE", "advertise start %d", rc);
}
static void sync_host(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc)
        rc = ble_hs_id_infer_auto(0, &address_type);
    if (!rc)
        advertise();
    else
        ESP_LOGE("BLE", "address init %d", rc);
}
static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}
static void command_task(void *arg)
{
    (void)arg;
    ble_command_t command;
    for (;;)
        if (xQueueReceive(commands, &command, portMAX_DELAY) == pdTRUE) {
            esp_err_t e = station_apply_json(command.text, command.length);
            if (e != ESP_OK) {
                char msg[64];
                snprintf(msg, sizeof(msg), "BLE command: %s", esp_err_to_name(e));
                station_status(msg);
                ESP_LOGW("BLE", "command rejected: %s", esp_err_to_name(e));
            }
        }
}
static void notify_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint16_t conn = connection;
        if (conn == BLE_HS_CONN_HANDLE_NONE || !subscribed)
            continue;
        char json[640];
        size_t n = station_json(json, sizeof(json));
        uint16_t mtu = ble_att_mtu(conn);
        if (!n || mtu < 3 || n > (size_t)(mtu - 3))
            continue;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(json, n);
        if (om) {
            int rc = ble_gatts_notify_custom(conn, tx_handle, om);
            if (rc)
                ESP_LOGD("BLE", "notify %d", rc);
        }
    }
}
esp_err_t station_start_ble(void)
{
    commands = xQueueCreate(4, sizeof(ble_command_t));
    if (!commands)
        return ESP_ERR_NO_MEM;
    esp_err_t e = nimble_port_init();
    if (e != ESP_OK)
        return e;
    ble_hs_cfg.sync_cb = sync_host;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set("HoloStationV3");
    if (rc)
        return ESP_FAIL;
    if (ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services))
        return ESP_FAIL;
    ble_att_set_preferred_mtu(517);
    if (xTaskCreate(command_task, "ble_commands", 6144, NULL, 3, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(notify_task, "ble_notify", 4096, NULL, 2, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
