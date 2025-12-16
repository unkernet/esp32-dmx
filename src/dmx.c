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
#define DMX_QUEUE_LEN     1
#define DMX_BREAK_BITS    22     // 22 * 4 мкс ≈ 88 мкс (250 кбит)
#define DMX_MAB_US        12     // минимум 8 мкс, берём с запасом
#define DMX_RX_BUFFERS    2

static const char *TAG = "DMX";

static uint8_t rx_buf[DMX_RX_BUFFERS][DMX_BUF_SIZE];
static app_config_t *app_config;

typedef struct {
    size_t len;
    uint8_t data[DMX_BUF_SIZE];
} dmx_tx_frame_t;

typedef struct {
    uint8_t *data;
    size_t   len;
} dmx_rx_frame_t;

static QueueHandle_t rx_queue;
static QueueHandle_t tx_queue;
static QueueHandle_t uart_evt_queue;

static void dmx_consumer_task(void *arg)
{
    dmx_rx_frame_t frame;

    while (1) {
        if (xQueueReceive(rx_queue, &frame, portMAX_DELAY)) {
            if (frame.len > 1 && frame.data[0] == 0) {
                size_t len = frame.len - 1;
                const uint8_t *data = frame.data + 1;
                uint8_t universe = app_config->dmx_in_universe;

                send_ws_dmx_data(universe, data, len);
                send_ambitful_dmx_data(universe, data, len);
                send_dmx_data(universe, data, len);
                send_artnet_dmx_data(universe, data, len, 0);
                send_ws2812_data(universe, data, len);
            }
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
                uint8_t dump[32];
                size_t left = evt.size;

                while (left > 0) {
                    int n = uart_read_bytes(
                        DMX_UART_NUM,
                        dump,
                        left > sizeof(dump) ? sizeof(dump) : left,
                        0);
                    left -= n;
                }
                break;
            }

            size_t to_read = evt.size;
            if (pos + to_read > DMX_BUF_SIZE)
                to_read = DMX_BUF_SIZE - pos;

            if (to_read) {
                pos += uart_read_bytes(
                    DMX_UART_NUM,
                    rx_buf[write_idx] + pos,
                    to_read,
                    0);
            }

            if (pos >= DMX_BUF_SIZE) {
                dmx_rx_frame_t frame = {
                    .data = rx_buf[write_idx],
                    .len  = pos
                };

                xQueueSend(rx_queue, &frame, 0);

                write_idx ^= 1;

                in_frame = false;
                pos = 0;
            }

            break;

        case UART_BREAK:
            if (in_frame && pos > 0) {
                dmx_rx_frame_t frame = {
                    .data = rx_buf[write_idx],
                    .len  = pos
                };

                xQueueSend(rx_queue, &frame, 0);

                write_idx ^= 1;
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
    dmx_tx_frame_t frame;

    while (1) {
        if (xQueueReceive(tx_queue, &frame, portMAX_DELAY)) {

            uart_write_bytes_with_break(
                DMX_UART_NUM,
                (const char *)frame.data,
                frame.len,
                DMX_BREAK_BITS
            );

            uart_wait_tx_done(DMX_UART_NUM, portMAX_DELAY);
        }
    }
}

void send_dmx_data(uint8_t universe, const uint8_t * data, uint16_t length)
{
    if (!app_config || universe != app_config->dmx_out_universe || length > DMX_BUF_SIZE - 1) {
        return;
    }

    dmx_tx_frame_t frame;
    frame.len = length + 1;
    frame.data[0] = 0;
    memcpy(frame.data + 1, data, length);

    if (xQueueSend(tx_queue, &frame, 0) != pdPASS) {
        ESP_LOGE(TAG, "tx queue overflow");
    }
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

    uart_driver_install(DMX_UART_NUM, 520, 0, 4, &uart_evt_queue, 0);
    uart_param_config(DMX_UART_NUM, &cfg);
    uart_set_pin(DMX_UART_NUM, DMX_TX_PIN, DMX_RX_PIN,
                 DMX_RTS_PIN, UART_PIN_NO_CHANGE);

    tx_queue = xQueueCreate(DMX_QUEUE_LEN, sizeof(dmx_tx_frame_t));

    xTaskCreate(
        dmx_rx_task,
        "dmx_rx",
        1024,
        NULL,
        7,
        NULL);

    xTaskCreate(
        dmx_consumer_task,
        "dmx_consumer",
        2048,
        NULL,
        5,
        NULL);

    xTaskCreate(
        dmx_tx_task,
        "dmx_tx",
        1536,
        NULL,
        5,
        NULL);

    return ESP_OK;
}
