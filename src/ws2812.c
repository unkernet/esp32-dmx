#include "esp_err.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ws2812.h"
#include "hardware_config.h"

#ifndef WS2812_PIN
#define WS2812_PIN -1
#endif

#define WS2812_RESET_US 75
#define MAX_DATA_LEN 512
static const char *TAG = "WS_2812";

static TaskHandle_t tx_task = NULL;
static SemaphoreHandle_t tx_sem;
static rmt_channel_handle_t rmt_chan;
static rmt_encoder_handle_t bytes_encoder;
static app_config_t *app_config;
static uint8_t *tx_data;
static size_t tx_len;

static void ws2812_tx_task(void *arg)
{
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        esp_err_t err = rmt_transmit(
            rmt_chan,
            bytes_encoder,
            tx_data,
            tx_len,
            &tx_cfg
        );
        if (err == ESP_OK) {
            rmt_tx_wait_all_done(rmt_chan, portMAX_DELAY);
            esp_rom_delay_us(WS2812_RESET_US);
        }
        xSemaphoreGive(tx_sem);
    }
}

void send_ws2812_data(uint16_t universe, const uint8_t * data, uint16_t length) {
    if (!app_config || app_config->ws2812_universe != universe) {
        return;
    }

    if (xSemaphoreTake(tx_sem, 0) != pdTRUE) {
        // ESP_LOGE(TAG, "tx queue overflow");
        return;
    }

    if (length > MAX_DATA_LEN) {
        length = MAX_DATA_LEN;
    }

    memcpy(tx_data, data, length);
    tx_len = length;
    xTaskNotifyGive(tx_task);
}

esp_err_t ws2812_init(app_config_t *config) {
    if ((config->enabled_modules & MOD_EN_WS2812) == 0) {
        ESP_LOGI(TAG, "disabled");
        return ESP_OK; // Disabled
    }

    RETURN_ON_NULL(tx_data = malloc(MAX_DATA_LEN), ESP_ERR_NO_MEM);

    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = WS2812_PIN,
        .mem_block_symbols = 64,
        .resolution_hz = 3200000, // 3.2 Mhz
        .trans_queue_depth = 4,
    };

    RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &rmt_chan));

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 1, // 312 ns
            .level1 = 0,
            .duration1 = 3, // 937 ns
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 3, // 937 ns
            .level1 = 0,
            .duration1 = 1, // 312 ns
        },
        .flags.msb_first = 1,
    };
    RETURN_ON_ERROR(rmt_new_bytes_encoder(&enc_cfg, &bytes_encoder));
    RETURN_ON_ERROR(rmt_enable(rmt_chan));

    RETURN_ON_NULL(tx_sem = xSemaphoreCreateBinary(), ESP_ERR_NO_MEM);
    xSemaphoreGive(tx_sem);
    xTaskCreate(ws2812_tx_task, "ws2812_tx", 1024, NULL, 7, &tx_task);
    RETURN_ON_NULL(tx_task, ESP_ERR_NO_MEM);

    app_config = config;

    return ESP_OK;
}
