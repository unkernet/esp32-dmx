#include "ambitful_ble.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "NIMBLE_BEACON";

#define MAX_AMBITFUL_GROUPS (8)
#define AMBITFUL_SIZE (8)
#define MAX_AMBITFUL_PRIORITY (8)
uint8_t groups_priority[MAX_AMBITFUL_GROUPS];
uint8_t ambitful_data[MAX_AMBITFUL_GROUPS * AMBITFUL_SIZE];
uint8_t last_transmitted_group = 0;
uint8_t counter = 0; // 0-222
uint8_t ibeacon_data[] = {
    0x4C, 0x00, 0x02, 0x15, 

    0xAB, 0 /* channel, group */, 3, 0,
    0x64, 0xFF,
    0xFF, 0xFF,
    0x00, 0x00,
    0x00, 0x11, 0x22, 0xBA, 0 /* counter */, 0 /* power */,

    0x00, 0x0A, 0x00, 0x6E, 0xC5 // mMajor, mMinor, mTxPower
};
static ble_addr_t ble_addr;
static app_config_t *app_config;

static void ble_app_advertise(void);
static void set_fields();

static void increment_counter() {
    if (++counter == 223) {
        counter = 0;
    }
    ble_hs_id_gen_rnd(0, &ble_addr);
}

// ////////
// Modes

static void power_mode(uint8_t mode) {
    // String.format("%02X%02X%02X00-64FF-FFFF-0000-001122%02X%02X",
    // (byte) -85, Integer.valueOf(this.ch * 10), 4, // 0
    // 64FF
    // FFFF
    // 0000
    // 00 11 22, (byte) -70, Integer.valueOf(this.id));

    // ibeacon_data[4] = 0xAB;
    ibeacon_data[5] = app_config->ambitful_channel * 10;
    ibeacon_data[6] = mode;
    ibeacon_data[7] = 0;

    ibeacon_data[8] = 0x64;
    ibeacon_data[9] = 0xff;

    ibeacon_data[10] = 0xff;
    ibeacon_data[11] = 0xff;

    ibeacon_data[10] = 0;
    ibeacon_data[11] = 0;

    ibeacon_data[12] = 0;
    ibeacon_data[13] = 0x11;
    ibeacon_data[14] = 0x22;
    // ibeacon_data[15] = 0xBA;
    ibeacon_data[16] = counter;
    ibeacon_data[17] = 0; // Power
}

static void mode_off() {
  power_mode(3);
}
static void mode_on() {
  power_mode(4);
}

static void mode_cct(uint8_t group, uint8_t power, uint8_t cct, uint8_t cctType, uint8_t rg) { // mode 0
    // tring.format("%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%04X%02X%02X%02X",
    // (byte) -85, Integer.valueOf(i3), Integer.valueOf(getMode()), Integer.valueOf(getCCT()),
    // Integer.valueOf(power), Integer.valueOf(getCCTType()),
    // 0, Integer.valueOf(getRG()),
    // 0, 0,
    // Integer.valueOf(getMode()), 2, (byte) -70, Integer.valueOf(this.id));

    power = (power * 101) >> 8; // 0-100
    cct = ((cct * 61) >> 8) + 25; // 25 - 85
    rg = (power * 21) >> 8; // 0-20

    // ibeacon_data[4] = 0xAB;
    ibeacon_data[5] = app_config->ambitful_channel * 10 + group + 1;
    ibeacon_data[6] = 0; // mode
    ibeacon_data[7] = cct;

    ibeacon_data[8] = power;
    ibeacon_data[9] = 100; // cctType = custom

    ibeacon_data[10] = 0;
    ibeacon_data[11] = rg;

    ibeacon_data[10] = 0;
    ibeacon_data[11] = 0;

    ibeacon_data[12] = 0;
    ibeacon_data[13] = 0; // mode
    ibeacon_data[14] = 2;
    // ibeacon_data[15] = 0xBA;
    ibeacon_data[16] = counter;
    ibeacon_data[17] = power;
}

static void mode_hsl(uint8_t group, uint8_t power, uint8_t h, uint8_t s) { // mode 1
    // String.format("%02X%02X%02X%02X-%02X%02X-%02X%02X-%04X-%02X%02X%02X%02X%02X",
    // (byte) -85, Integer.valueOf(i3), Integer.valueOf(getMode()), 0,
    // Integer.valueOf(power), 0,
    // 0, 0,
    // Integer.valueOf(getHue()),
    // Byte.valueOf((byte) getSta()), Integer.valueOf(getMode()), 2, (byte) -70, Integer.valueOf(this.id));

    power = (power * 101) >> 8; // 0-100
    s = (s * 101) >> 8; // 0-100
    uint16_t hue = (h * 180) >> 7; // 0 - 359

    // ibeacon_data[4] = 0xAB;
    ibeacon_data[5] = app_config->ambitful_channel * 10 + group + 1;
    ibeacon_data[6] = 1; // mode
    ibeacon_data[7] = 0;

    ibeacon_data[8] = power;
    ibeacon_data[9] = 0;

    ibeacon_data[10] = 0;
    ibeacon_data[11] = 0;

    ibeacon_data[10] = hue >> 8;
    ibeacon_data[11] = hue;

    ibeacon_data[12] = s;
    ibeacon_data[13] = 1; // mode
    ibeacon_data[14] = 2;
    // ibeacon_data[15] = 0xBA;
    ibeacon_data[16] = counter;
    ibeacon_data[17] = power;
}

static void mode_fx(uint8_t group, uint8_t power, uint8_t scene, uint8_t speed) { // mode 2
    // String.format("%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%04X%02X%02X%02X",
    // (byte) -85, Integer.valueOf(i3), Integer.valueOf(getMode()), Integer.valueOf(getSceneID()),
    // Integer.valueOf(power), Integer.valueOf(getSpeed() + 1),
    // Integer.valueOf(getSceneID()), 0,
    // 0, 0,
    // Integer.valueOf(getMode()), 2, (byte) -70, Integer.valueOf(this.id));

    power = (power * 101) >> 8; // 0-100
    scene = scene / 10; // 0-25
    speed = ((speed * 3) >> 8) + 1; // 1-3

    // ibeacon_data[4] = 0xAB;
    ibeacon_data[5] = app_config->ambitful_channel * 10 + group + 1;
    ibeacon_data[6] = 2; // mode
    ibeacon_data[7] = scene;

    ibeacon_data[8] = power;
    ibeacon_data[9] = speed;

    ibeacon_data[10] = scene;
    ibeacon_data[11] = 0;

    ibeacon_data[10] = 0;
    ibeacon_data[11] = 0;

    ibeacon_data[12] = 0;
    ibeacon_data[13] = 2; // mode
    ibeacon_data[14] = 2;
    // ibeacon_data[15] = 0xBA;
    ibeacon_data[16] = counter;
    ibeacon_data[17] = power;
}

static void mode_rgb(uint8_t group, uint8_t power, uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t y) { // mode 5
    // str = String.format("%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%04X%02X%02X%02X",
    // (byte) -85, Integer.valueOf(i3), Integer.valueOf(getMode()), 0,
    // Integer.valueOf(power), Integer.valueOf(getR()),
    // Integer.valueOf(getG()), Integer.valueOf(getB()),
    // Integer.valueOf(getW()), Integer.valueOf(getY()),
    // Integer.valueOf(getMode()), 2, (byte) -70, Integer.valueOf(this.id));

    // ibeacon_data[4] = 0xAB;
    ibeacon_data[5] = app_config->ambitful_channel * 10 + group + 1;
    ibeacon_data[6] = 5; // mode
    ibeacon_data[7] = 0;

    ibeacon_data[8] = power; // 0-100
    ibeacon_data[9] = r; // 0-100

    ibeacon_data[10] = g; // 0-100
    ibeacon_data[11] = b; // 0-100

    ibeacon_data[10] = w; // 0-100
    ibeacon_data[11] = y; // 0-100

    ibeacon_data[12] = 0;
    ibeacon_data[13] = 5; // mode
    ibeacon_data[14] = 2;
    // ibeacon_data[15] = 0xBA;
    ibeacon_data[16] = counter;
    ibeacon_data[17] = power; // 0-100
}

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
    fields.mfg_data = ibeacon_data;
    fields.mfg_data_len = sizeof(ibeacon_data);

    int rc;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error ble_gap_adv_set_fields; rc=%d", rc);
    } else {
        // ESP_LOGI(TAG, "ble_gap_adv_set_fields successfully");
    }
    // ble_addr.val[0]++;
    rc = ble_hs_id_set_rnd(ble_addr.val);
    if (rc != 0) {
        ESP_LOGE(TAG, "error ble_hs_id_set_rnd; rc=%d", rc);
    } else {
        // ESP_LOGI(TAG, "ble_hs_id_set_rnd successfully %02x %02x %02x %02x %02x %02x", ble_addr.val[0]
        // , ble_addr.val[1], ble_addr.val[2], ble_addr.val[3], ble_addr.val[4], ble_addr.val[5]);
    }
}

/* GAP event handler */
static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_ADV_COMPLETE:

            uint8_t max_priority = 0;
            uint8_t ambitful_groups = app_config->ambitful_groups;
            uint8_t group;
            uint8_t * group_data;
            for (group = 0; group < ambitful_groups; group ++) {
                if (max_priority < groups_priority[group]) {
                    max_priority = groups_priority[group];
                }
            }
            for (group = last_transmitted_group + 1; group != last_transmitted_group; group++) {
                if (group >= ambitful_groups) {
                    group = 0;
                }
                if (max_priority == groups_priority[group]) {
                    break;
                }
            }
            last_transmitted_group = group;
            group_data = &ambitful_data[group * AMBITFUL_SIZE];

            uint8_t mode = group_data[0];
            if (mode < 64) {
                mode_rgb(group, group_data[1], group_data[2], group_data[3], group_data[4], group_data[5], group_data[6]);
            } else if (mode < 64 * 2) {
                mode_hsl(group, group_data[1], group_data[2], group_data[3]);
            } else if (mode < 64 * 3) {
                mode_cct(group, group_data[1], group_data[2], group_data[3], group_data[4]);
            } else {
                mode_fx(group, group_data[1], group_data[2], group_data[3]);
            }

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
        .itvl_min = BLE_GAP_ADV_ITVL_MS(app_config->ble_interval),
        .itvl_max = BLE_GAP_ADV_ITVL_MS(app_config->ble_interval),
    };

    int rc;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, app_config->ble_duration_ms,
                      &params, gap_event, NULL);

    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
        return;
    }
    // ESP_LOGI(TAG, "Advertising started successfully with interval %dms and duration %dms.",
    //          app_config->ble_interval, app_config->ble_duration_ms);
}

void ble_beacon_task(void *param)
{
    nimble_port_run(); // This function will return only when nimble_port_stop() is called
    nimble_port_freertos_deinit();
}

esp_err_t ambitful_ble_init(app_config_t *config)
{
    if (!config->ambitful_groups) {
        return ESP_OK;
    }
    if (config->ambitful_groups > MAX_AMBITFUL_GROUPS) {
        config->ambitful_groups = MAX_AMBITFUL_GROUPS;
    }
    app_config = config; // Store config globally
    memset(groups_priority, 0, sizeof(groups_priority));
    memset(ambitful_data, 0, sizeof(ambitful_data));

    nimble_port_init();

    ble_hs_cfg.sync_cb = ble_app_on_sync;

    mode_on();

    nimble_port_freertos_init(ble_beacon_task);

    return ESP_OK;
}

void send_ambitful_dmx_data(uint8_t universe, const uint8_t * data, uint16_t length) {
    uint8_t ambitful_groups = app_config->ambitful_groups;
    if (ambitful_groups > MAX_AMBITFUL_GROUPS) {
        ambitful_groups = MAX_AMBITFUL_GROUPS;
    }
    if (app_config == NULL || app_config->ambitful_channel == 0 ||  ambitful_groups == 0 || app_config->ambitful_universe != universe
        || length < (uint16_t)(app_config->ambitful_addr + ambitful_groups * AMBITFUL_SIZE)) {
        return;
    }
    data += app_config->ambitful_addr;
    uint8_t changed = 0;
    for (uint8_t i = 0; i < ambitful_groups; i++) {
        if (memcmp(ambitful_data + i * AMBITFUL_SIZE, data + i * AMBITFUL_SIZE, AMBITFUL_SIZE) != 0) {
            memcpy(ambitful_data + i * AMBITFUL_SIZE, data + i * AMBITFUL_SIZE, AMBITFUL_SIZE);
            groups_priority[i] = MAX_AMBITFUL_PRIORITY;
            changed = 1;
        }
    }
    if (changed) {
        increment_counter();
    }
}
