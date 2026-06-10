#include "esp_err.h"
#include "esp_idf_version.h"
#include "driver/rmt_tx.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/param.h"
#include "ws2812.h"
#include "modules.h"

#define WS2812_RESET_US 75
static const char *TAG = "WS_2812";

#if ESP_IDF_VERSION_MAJOR > 5
#include "hal/rmt_ll.h"
#define SOC_RMT_TX_CANDIDATES_PER_GROUP RMT_LL_TX_CANDIDATES_PER_INST
#define SOC_RMT_GROUPS RMT_LL_INST_NUM
#endif

#define RMT_TX_NUM_MAX    (SOC_RMT_TX_CANDIDATES_PER_GROUP * SOC_RMT_GROUPS)
#define WS2812_PORT_COUNT_MAX    MIN(4, RMT_TX_NUM_MAX)

typedef struct {
    rmt_channel_handle_t rmt_chan;
    rmt_encoder_handle_t bytes_encoder;
    TaskHandle_t tx_task;
    SemaphoreHandle_t tx_sem;
    uint8_t *tx_data;
    size_t tx_len;
    uint16_t universe;
} ws2812_port_t;

static ws2812_port_t *registered_ports[WS2812_PORT_COUNT_MAX] = {0};
static uint8_t registered_port_count = 0;
static app_config_t *app_config;

static void ws2812_tx_task(void *arg)
{
    ws2812_port_t *port = (ws2812_port_t *)arg;
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        esp_err_t err = rmt_transmit(
            port->rmt_chan,
            port->bytes_encoder,
            port->tx_data,
            port->tx_len,
            &tx_cfg
        );
        if (err == ESP_OK) {
            rmt_tx_wait_all_done(port->rmt_chan, portMAX_DELAY);
            esp_rom_delay_us(WS2812_RESET_US);
        }
        xSemaphoreGive(port->tx_sem);
    }
}

void send_ws2812_data(uint16_t universe, const uint8_t * data, uint16_t length) {
    if (!app_config) return;

    for (int i = 0; i < registered_port_count; i++) {
        ws2812_port_t *port = registered_ports[i];
        if (port && port->universe == universe) {
            if (xSemaphoreTake(port->tx_sem, 0) != pdTRUE) {
                continue;
            }

            size_t copy_len = MIN(WS2812_LEN, length);
            memcpy(port->tx_data, data, copy_len);
            port->tx_len = copy_len;
            xTaskNotifyGive(port->tx_task);
        }
    }
}

static esp_err_t ws2812_init_port(ws2812_settings_t *settings, int gpio_num) {
    if (registered_port_count >= WS2812_PORT_COUNT_MAX) {
        ESP_LOGE(TAG, "Hardware RMT channel limit reached");
        return ESP_ERR_NOT_SUPPORTED;
    }

    ws2812_port_t *port = calloc(1, sizeof(ws2812_port_t));
    RETURN_ON_NULL(port, ESP_ERR_NO_MEM);

    port->universe = settings->universe;

    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = gpio_num,
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
        .resolution_hz = 3200000, // 3.2 Mhz
        .trans_queue_depth = 4,
    };

    RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &port->rmt_chan));

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
    
    RETURN_ON_ERROR(rmt_new_bytes_encoder(&enc_cfg, &port->bytes_encoder));
    RETURN_ON_ERROR(rmt_enable(port->rmt_chan));

    RETURN_ON_NULL(port->tx_sem = xSemaphoreCreateBinary(), ESP_ERR_NO_MEM);
    xSemaphoreGive(port->tx_sem);

    RETURN_ON_NULL(port->tx_data = malloc(WS2812_LEN), ESP_ERR_NO_MEM);

    char task_name[16];
    snprintf(task_name, sizeof(task_name), "ws2812_tx_%d", registered_port_count);
    xTaskCreate(ws2812_tx_task, task_name, 2048, port, 7, &port->tx_task);
    RETURN_ON_NULL(port->tx_task, ESP_ERR_NO_MEM);

    registered_ports[registered_port_count++] = port;
    return ESP_OK;
}

esp_err_t ws2812_init(app_config_t *config) {

    #ifdef WS2812_0_PIN
    if (config->enabled_modules & MOD_EN_WS2812_0) {
        ws2812_init_port(&config->ws2812_ports[0], WS2812_0_PIN);
    }
    #endif

    #ifdef WS2812_1_PIN
    #if RMT_TX_NUM_MAX < 2
    #error "Hardware RMT channel limit reached"
    #endif
    if (config->enabled_modules & MOD_EN_WS2812_1) {
        ws2812_init_port(&config->ws2812_ports[1], WS2812_1_PIN);
    }
    #endif

    #ifdef WS2812_2_PIN
    #if RMT_TX_NUM_MAX < 3
    #error "Hardware RMT channel limit reached"
    #endif
    if (config->enabled_modules & MOD_EN_WS2812_2) {
        ws2812_init_port(&config->ws2812_ports[2], WS2812_2_PIN);
    }
    #endif

    #ifdef WS2812_3_PIN
    #if RMT_TX_NUM_MAX < 4
    #error "Hardware RMT channel limit reached"
    #endif
    if (config->enabled_modules & MOD_EN_WS2812_3) {
        ws2812_init_port(&config->ws2812_ports[3], WS2812_3_PIN);
    }
    #endif

    app_config = config;

    return ESP_OK;
}
