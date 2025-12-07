#include "ble_beacon.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "NIMBLE_BEACON";

static uint8_t g_beacon_data[31];
static ble_addr_t ble_addr;
static uint8_t g_beacon_data_len = 0;

static void ble_app_advertise(void);
static void set_fields();
static void ble_app_on_sync(void)
{
    ESP_LOGI(TAG, "BLE host synchronized.");
    ble_hs_id_gen_rnd(0, &ble_addr);
    set_fields();
    ble_app_advertise();
}

static void set_fields() {
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.mfg_data = g_beacon_data;                // (FF 00 counter)
    fields.mfg_data_len = g_beacon_data_len;

    int rc;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error ble_gap_adv_set_fields; rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "ble_gap_adv_set_fields successfully");
    }
    // ble_addr_t ble_addr;
    ble_addr.val[0]++;
    // rc = ble_hs_id_gen_rnd(0, &ble_addr);
    rc = ble_hs_id_set_rnd(ble_addr.val);
    if (rc != 0) {
        ESP_LOGE(TAG, "error ble_hs_id_set_rnd; rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "ble_hs_id_set_rnd successfully %02x %02x %02x %02x %02x %02x", ble_addr.val[0]
        , ble_addr.val[1], ble_addr.val[2], ble_addr.val[3], ble_addr.val[4], ble_addr.val[5]);
    }
}

/* GAP event handler */
static int gap_event(struct ble_gap_event *event, void *arg)
{
    uint8_t * counter = &g_beacon_data[18];
    switch (event->type) {

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "adv event transmitted (counter=%u size=%u)", *counter, g_beacon_data_len);

        (*counter)++;

        set_fields();
        ble_app_advertise();
        return 0;

    default:
        return 0;
    }
}

static void ble_app_advertise(void)
{
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_NON,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = 0x20, /* 50 ms */
        .itvl_max = 0x20,
    };

    int rc;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, (250),
                      &params, gap_event, NULL);

    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "Advertising started successfully.");
}

void ble_beacon_task(void *param)
{
    nimble_port_run(); // This function will return only when nimble_port_stop() is called
    nimble_port_freertos_deinit();
}

esp_err_t ble_beacon_set_data(const uint8_t* data, uint8_t data_len)
{
    if (data_len > sizeof(g_beacon_data)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(g_beacon_data, data, data_len);
    g_beacon_data_len = data_len;
    // set_fields();
    // ble_app_advertise();
    return ESP_OK;
}

esp_err_t ble_beacon_init(void)
{
    nimble_port_init();

    ble_hs_cfg.sync_cb = ble_app_on_sync;

    // <Buffer 4c 00 02 15
    // ab 14 03 00 64 ff ff ff
    // 00 00 00 11 22 ba 09 07
    // 00 0a 00 6e c5>
    // Default iBeacon data
    uint8_t ibeacon_data[] = {
        0x4C, 0x00, 0x02, 0x15, 
        0xAB, 0xC5, 0x6D, 0xB5, 0xDF, 0xFB, 0x48, 0xD2, 
        0xB0, 0x60, 0xD0, 0xF5, 0xA7, 0xBA, 0x11, 0x07, 
        0x00, 0x0A, 0x00, 0x6E, 0xC5
    };
    ble_beacon_set_data(ibeacon_data, sizeof(ibeacon_data));

    nimble_port_freertos_init(ble_beacon_task);

    return ESP_OK;
}