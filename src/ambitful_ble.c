#include "ambitful_ble.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_mac.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modules.h"

static const char *TAG = "AMBUTFUL";

#define MAX_AMBITFUL_GROUPS (8)
#define AMBITFUL_SIZE (8)
#define MAX_AMBITFUL_PRIORITY (4)
#define IDLE_SLOW_DOWN (8)

#define BLE_INTERVAL_MS 25
#define BLE_DURATION_MS 80

static uint8_t groups_priority[MAX_AMBITFUL_GROUPS];
static uint8_t ambitful_data[MAX_AMBITFUL_GROUPS * AMBITFUL_SIZE];
static uint8_t ambitful_last_transmitted_group = 0;
static uint8_t ambitful_idle_mode = 1;
static SemaphoreHandle_t s_ble_data_mutex;
static TaskHandle_t advertise_task;

// static uint8_t counter = 0; // 0-222
static uint8_t ibeacon_data[] = {
    // Header
    0x4C, 0x00, 0x02, 0x15, 
    // Body
    0xAB, 0 /* channel, group */, 3 /* mode */, 0,
    0x64, 0xFF,
    0xFF, 0xFF,
    0x00, 0x00,
    0x00, 0x11, 0x22, 0xBA, 0 /* counter */, 0 /* power */,
    // Footer
    0x00, 0x0A, 0x00, 0x6E, 0xC5 // mMajor, mMinor, mTxPower
};
static uint8_t ble_addr[6];
static app_config_t *app_config;

static void ble_app_advertise(void);
static void set_fields();

static void increment_counter() {
    if (++ibeacon_data[18] == 223) {
        ibeacon_data[18] = 0;
    }
}

static inline uint8_t clamp_100(uint8_t v) {
    // 0 -> 0
    // 1 -> 1
    // 255 -> 100
    return ((v + 2) * 100) >> 8;
}

// ////////
// Modes

static void power_mode(uint8_t mode) {
    // ibeacon_data[4] = 0xAB;
    ibeacon_data[5] = app_config->ambitful_channel * 10;
    ibeacon_data[6] = mode;
    ibeacon_data[7] = 0;

    ibeacon_data[8] = 0x64;
    ibeacon_data[9] = 0xff;

    ibeacon_data[10] = 0xff;
    ibeacon_data[11] = 0xff;

    ibeacon_data[12] = 0;
    ibeacon_data[13] = 0;

    ibeacon_data[14] = 0;
    ibeacon_data[15] = 0x11;
    ibeacon_data[16] = 0x22;
    // ibeacon_data[17] = 0xBA;
    // ibeacon_data[18] = counter;
    ibeacon_data[19] = 0; // Power
}

static void mode_off() {
  power_mode(3);
}
static void mode_on() {
  power_mode(4);
}

static void mode_cct(uint8_t group, uint8_t power, uint8_t cct, uint8_t rg) { // mode 0
    power = clamp_100(power); // 0 - 100
    cct = ((cct * 61) >> 8) + 25; // 25 - 85
    rg = (rg * 21) >> 8; // 0-20

    // ibeacon_data[4] = 0xAB;
    ibeacon_data[5] = app_config->ambitful_channel * 10 + group + 1;
    ibeacon_data[6] = 0; // mode
    ibeacon_data[7] = cct;

    ibeacon_data[8] = power;
    ibeacon_data[9] = 100; // cctType = custom

    ibeacon_data[10] = 0;
    ibeacon_data[11] = rg;

    ibeacon_data[12] = 0;
    ibeacon_data[13] = 0;

    ibeacon_data[14] = 0;
    ibeacon_data[15] = 0; // mode
    ibeacon_data[16] = 2;
    // ibeacon_data[17] = 0xBA;
    // ibeacon_data[18] = counter;
    ibeacon_data[19] = power;
}

static void mode_hsl(uint8_t group, uint8_t power, uint8_t h, uint8_t s) { // mode 1
    power = clamp_100(power); // 0 - 100
    s = clamp_100(s); // 0-100
    uint16_t hue = ((uint32_t)h * 361) >> 8; // 0 - 359

    // ibeacon_data[4] = 0xAB;
    ibeacon_data[5] = app_config->ambitful_channel * 10 + group + 1;
    ibeacon_data[6] = 1; // mode
    ibeacon_data[7] = 0;

    ibeacon_data[8] = power;
    ibeacon_data[9] = 0;

    ibeacon_data[10] = 0;
    ibeacon_data[11] = 0;

    ibeacon_data[12] = hue >> 8;
    ibeacon_data[13] = hue;

    ibeacon_data[14] = s;
    ibeacon_data[15] = 1; // mode
    ibeacon_data[16] = 2;
    // ibeacon_data[17] = 0xBA;
    // ibeacon_data[18] = counter;
    ibeacon_data[19] = power;
}

static void mode_fx(uint8_t group, uint8_t power, uint8_t scene, uint8_t speed) { // mode 2
    // String.format("%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%04X%02X%02X%02X",
    // (byte) -85, Integer.valueOf(i3), Integer.valueOf(getMode()), Integer.valueOf(getSceneID()),
    // Integer.valueOf(power), Integer.valueOf(getSpeed() + 1),
    // Integer.valueOf(getSceneID()), 0,
    // 0, 0,
    // Integer.valueOf(getMode()), 2, (byte) -70, Integer.valueOf(this.id));

    power = clamp_100(power); // 0 - 100
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

    ibeacon_data[12] = 0;
    ibeacon_data[13] = 0;

    ibeacon_data[14] = 0;
    ibeacon_data[15] = 2; // mode
    ibeacon_data[16] = 2;
    // ibeacon_data[17] = 0xBA;
    // ibeacon_data[18] = counter;
    ibeacon_data[19] = power;
}

static void mode_rgb(uint8_t group, uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t y) { // mode 5
    r = clamp_100(r); // 0-100
    g = clamp_100(g); // 0-100
    b = clamp_100(b); // 0-100
    w = clamp_100(w); // 0-100
    y = clamp_100(y); // 0-100

    uint8_t power = r;
    if (power < g) power = g;
    if (power < b) power = b;
    if (power < w) power = w;
    if (power < y) power = y;

    // ibeacon_data[4] = 0xAB;
    ibeacon_data[5] = app_config->ambitful_channel * 10 + group + 1;
    ibeacon_data[6] = 5; // mode
    ibeacon_data[7] = 0;

    ibeacon_data[8] = power;
    ibeacon_data[9] = r;

    ibeacon_data[10] = g;
    ibeacon_data[11] = b;

    ibeacon_data[12] = w;
    ibeacon_data[13] = y;

    ibeacon_data[14] = 0;
    ibeacon_data[15] = 5; // mode
    ibeacon_data[16] = 2;
    // ibeacon_data[17] = 0xBA;
    // ibeacon_data[18] = counter;
    ibeacon_data[19] = power;
}

// Set mac address depending on control group
static void set_ble_mac(uint8_t group) {
    uint8_t mac[6];
    memcpy(mac, ble_addr, 6);
    mac[0] += group;
    ble_hs_id_set_rnd(mac);
}

static void ble_app_on_sync(void)
{
    ESP_LOGI(TAG, "BLE host synchronized.");
    if (app_config) {
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    }
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
    }
}

static void adv_next_group() {
    xSemaphoreTake(s_ble_data_mutex, portMAX_DELAY);

    uint8_t max_priority = 0;
    uint8_t ambitful_groups = app_config->ambitful_groups;
    uint8_t group;
    uint8_t * group_data;
    // First, we need to find a group with max priproty
    for (group = 0; group < ambitful_groups; group ++) {
        if (max_priority < groups_priority[group]) {
            max_priority = groups_priority[group];
        }
    }
    // Second pass, we look for the group with max_priority, starting after the last transmitted one
    // to ensure round-robin behavior when priorities are equal.
    group = (ambitful_last_transmitted_group + 1) % ambitful_groups;
    for (int i = 0; i < ambitful_groups; i++) {
        if (groups_priority[group] == max_priority) {
            break; // Found a candidate
        }
        group = (group + 1) % ambitful_groups; // Move to next group, wrapping around
    }
    if (groups_priority[group] > 0) {
        --groups_priority[group];
    } 
    if (max_priority == 0) {
        ambitful_idle_mode = 1;
    }
    ambitful_last_transmitted_group = group;
    group_data = &ambitful_data[group * AMBITFUL_SIZE];

    uint8_t mode = group_data[0];
    if (mode < 64) {
        // R, G, B, W, Y
        mode_rgb(group, group_data[1], group_data[2], group_data[3], group_data[4], group_data[5]);
    } else if (mode < 64 * 2) {
        // Power, H, S
        mode_hsl(group, group_data[1], group_data[2], group_data[3]);
    } else if (mode < 64 * 3) {
        // Power, CCT, Rg
        mode_cct(group, group_data[1], group_data[2], group_data[3]);
    } else {
        // Power, Scene, Speed
        mode_fx(group, group_data[1], group_data[2], group_data[3]);
    }

    xSemaphoreGive(s_ble_data_mutex);

    set_ble_mac(group);
    set_fields();
    ble_app_advertise();
}

/* GAP event handler */
static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_ADV_COMPLETE:
            adv_next_group();
            return 0;

        default:
            return 0;
    }
}

static void ble_app_advertise(void)
{
    // In idle mode, when there are no changes for a same time, make advertisement IDLE_SLOW_DOWN slower
    uint8_t mult = ambitful_idle_mode * IDLE_SLOW_DOWN + 1;
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_NON,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = BLE_GAP_ADV_ITVL_MS(BLE_INTERVAL_MS * mult),
        .itvl_max = BLE_GAP_ADV_ITVL_MS(BLE_INTERVAL_MS * mult),
    };
    // ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_DURATION_MS * mult, &params, gap_event, NULL);
    ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_DURATION_MS * mult, &params, gap_event, NULL);
}

void ble_beacon_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void restart_advertise_task(void *arg) {
    while (1) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            ble_gap_adv_stop();
            adv_next_group();
        }
    }
}

esp_err_t ambitful_ble_init(app_config_t *config)
{
    if ((config->enabled_modules & MOD_EN_AMBITFUL) == 0 || !config->ambitful_groups) {
        ESP_LOGI(TAG, "disabled");
        return ESP_OK;
    }
    if (config->ambitful_groups > MAX_AMBITFUL_GROUPS) {
        config->ambitful_groups = MAX_AMBITFUL_GROUPS;
    }
    if (config->ambitful_addr + config->ambitful_groups * AMBITFUL_SIZE >= DMX_LEN) {
        return ESP_ERR_INVALID_SIZE; // Invalid configuration
    }
    memset(groups_priority, 0, sizeof(groups_priority));
    memset(ambitful_data, 0, sizeof(ambitful_data));
    RETURN_ON_ERROR(esp_read_mac(ble_addr, ESP_MAC_WIFI_STA));
    for (uint8_t i = 0; i < 3; i++) {
        // Reverse MAC address for BLE
        ble_addr[i] ^= ble_addr[5 - i];
        ble_addr[5 - i] ^= ble_addr[i];
        ble_addr[i] ^= ble_addr[5 - i];
    }
    ble_addr[5] &= 0x3f; // Non-Resolvable Private Address (NRPA)
    RETURN_ON_NULL(s_ble_data_mutex = xSemaphoreCreateMutex(), ESP_ERR_NO_MEM);
    xTaskCreate(restart_advertise_task, "ble_adv", 2048, NULL, 5, &advertise_task);
    RETURN_ON_NULL(advertise_task, ESP_ERR_NO_MEM);

    RETURN_ON_ERROR(nimble_port_init());

    ble_hs_cfg.sync_cb = ble_app_on_sync;

    nimble_port_freertos_init(ble_beacon_task);

    app_config = config;

    return ESP_OK;
}

void send_ambitful_dmx_data(uint16_t universe, const uint8_t * data, uint16_t length) {
    if (app_config == NULL || app_config->ambitful_universe != universe) {
        return;
    }

    const uint8_t groups = app_config->ambitful_groups;
    const uint16_t addr = app_config->ambitful_addr;

    if (length < addr + (groups * AMBITFUL_SIZE)) {
        return;
    }

    if (xSemaphoreTake(s_ble_data_mutex, 0) != pdTRUE) {
        return;
    }

    const uint8_t *src = data + addr;
    uint8_t *dst = ambitful_data;
    uint8_t changed = 0;

    for (uint8_t i = 0; i < groups; i++, src += AMBITFUL_SIZE, dst += AMBITFUL_SIZE) {
        if (memcmp(dst, src, AMBITFUL_SIZE) != 0) {
            memcpy(dst, src, AMBITFUL_SIZE);
            groups_priority[i] = MAX_AMBITFUL_PRIORITY;
            changed = 1;
        }
    }

    if (changed) {
        increment_counter();
        if (ambitful_idle_mode) {
            ambitful_idle_mode = 0;
            xTaskNotifyGive(advertise_task);
        }
    }
    xSemaphoreGive(s_ble_data_mutex);
}
