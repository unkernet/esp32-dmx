#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "dmx.h"
#include <string.h>
#include "artnet_server.h"
#include "web_server.h"
#include "ambitful_ble.h"
#include "ws2812.h"

#define DMX_UART_NUM      UART_NUM_1
#define DMX_TX_PIN        4
#define DMX_RX_PIN        5
#define DMX_RTS_PIN       UART_PIN_NO_CHANGE   // если нужен DE — управляется отдельно
#define DMX_BUF_SIZE      513                  // start code + 512
#define DMX_BREAK_BITS    22     // 22 * 4 мкс ≈ 88 мкс (250 кбит)
#define DMX_RX_BUFFERS    2
#define MIN_DMX_LEN       30
#define RX_FREE           (-1)

static const char *TAG = "DMX";

static app_config_t *app_config;

typedef struct {
    size_t len;
    uint8_t data[DMX_BUF_SIZE];
} dmx_frame_t;

static TaskHandle_t tx_task = NULL, consumer_task = NULL;
static dmx_frame_t dmx_tx_buf;
static dmx_frame_t dmx_rx_buf[DMX_RX_BUFFERS];
static SemaphoreHandle_t tx_sem;
static QueueHandle_t uart_evt_queue;
static volatile int8_t rx_read_idx = RX_FREE;

static void dmx_consumer_task(void *arg)
{
    while (1) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            int8_t idx = rx_read_idx;
            if (idx == RX_FREE) {
                continue;
            }
            dmx_frame_t * frame = &dmx_rx_buf[idx];
            if (frame->len > 1 && frame->data[0] == 0) {
                size_t len = frame->len - 1;
                const uint8_t *data = frame->data + 1;
                uint8_t universe = app_config->dmx_in_universe;

                send_ws_dmx_data(universe, data, len);
                send_ambitful_dmx_data(universe, data, len);
                send_artnet_dmx_data(universe, data, len, 0);
                send_ws2812_data(universe, data, len);
                send_dmx_data(universe, data, len); // Allow passthrough?
            }
            rx_read_idx = RX_FREE; // Mark consumer as free
        }
    }
}

static void dmx_rx_task(void *arg)
{
    uart_event_t evt;
    uint8_t write_idx = 0;
    size_t pos = 0;
    bool in_frame = false;

    while (1) {
        if (!xQueueReceive(uart_evt_queue, &evt, portMAX_DELAY))
            continue;

        switch (evt.type) {

        case UART_DATA:
            if (!in_frame) {
                static uint8_t dump[32];
                size_t left = evt.size;
                ESP_LOGI(TAG, "UART_DATA dump %d", left);

                while (left > 0) {
                    int n = uart_read_bytes(DMX_UART_NUM, dump, left > sizeof(dump) ? sizeof(dump) : left, 0);
                    left -= n;
                }
                break;
            }

            size_t to_read = evt.size;
            if (pos + to_read > DMX_BUF_SIZE)
                to_read = DMX_BUF_SIZE - pos;

            if (to_read) {
                pos += uart_read_bytes(DMX_UART_NUM, dmx_rx_buf[write_idx].data + pos, to_read, 0);
            }

            if (pos >= DMX_BUF_SIZE) {
                ESP_LOGI(TAG, "UART_DATA read full %d", to_read);
                if (rx_read_idx == RX_FREE) {
                    dmx_rx_buf[write_idx].len = pos;
                    rx_read_idx = write_idx;
                    write_idx ^= 1;
                    xTaskNotifyGive(consumer_task);
                } else {
                    ESP_LOGE(TAG, "rx buffer full");
                }

                in_frame = false;
                pos = 0;
            } else {
                ESP_LOGI(TAG, "UART_DATA read %d", to_read);
            }

            break;

        case UART_BREAK:
            ESP_LOGI(TAG, "UART_BREAK %d", pos);
            if (in_frame && pos > MIN_DMX_LEN) {
                dmx_rx_buf[write_idx].len = pos;
                if (rx_read_idx == RX_FREE) {
                    rx_read_idx = write_idx;
                    write_idx ^= 1;
                    xTaskNotifyGive(consumer_task);
                } else {
                    ESP_LOGE(TAG, "rx buffer full");
                }
            }

            pos = 0;
            in_frame = true;
            break;

        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            uart_flush_input(DMX_UART_NUM);
            pos = 0;
            in_frame = false;
            ESP_LOGE(TAG, "UART buffer overflow");
            break;

        default:
            break;
        }
    }
}

static void dmx_tx_task(void *arg)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uart_write_bytes_with_break(DMX_UART_NUM, (const char *)dmx_tx_buf.data, dmx_tx_buf.len, DMX_BREAK_BITS);
        uart_wait_tx_done(DMX_UART_NUM, portMAX_DELAY);
        xSemaphoreGive(tx_sem);
    }
}

void send_dmx_data(uint8_t universe, const uint8_t * data, uint16_t length)
{
    if (!app_config || universe != app_config->dmx_out_universe || length > DMX_BUF_SIZE - 1) {
        return;
    }

    if (xSemaphoreTake(tx_sem, 0) != pdTRUE) {
        ESP_LOGE(TAG, "tx queue overflow");
        return;
    }

    dmx_tx_buf.len = length + 1;
    dmx_tx_buf.data[0] = 0;
    memcpy(dmx_tx_buf.data + 1, data, length);

    xTaskNotifyGive(tx_task);
}

esp_err_t dmx_init(app_config_t *config)
{
    app_config = config;

    uart_config_t cfg = {
        .baud_rate  = 250000,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_2,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(DMX_UART_NUM, 1024, 0, 4, &uart_evt_queue, 0);
    uart_param_config(DMX_UART_NUM, &cfg);
    uart_set_pin(DMX_UART_NUM, DMX_TX_PIN, DMX_RX_PIN, DMX_RTS_PIN, UART_PIN_NO_CHANGE);

    tx_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(tx_sem);

    xTaskCreate(dmx_rx_task, "dmx_rx", 2048, NULL, 7, NULL);
    xTaskCreate(dmx_consumer_task, "dmx_consumer", 2048, NULL, 5, &consumer_task);
    xTaskCreate(dmx_tx_task, "dmx_tx", 2048, NULL, 5, &tx_task);

    return ESP_OK;
}
