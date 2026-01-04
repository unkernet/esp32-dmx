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
#define DMX_RTS_PIN       UART_PIN_NO_CHANGE
#define DMX_BREAK_BITS    22 // 22 * 4 us ≈ 88 us
#define MIN_DMX_LEN       30

/*
 * On ESP32 UART (ESP-IDF), one extra zero byte is consistently observed
 * at the end of each frame when using UART_BREAK detection.
 * This byte is associated with the BREAK condition and is discarded
 * during processing.
 */
#define DMX_BUF_SIZE      514 // start code + 512 + break

static const char *TAG = "DMX";

static app_config_t *app_config;

typedef struct {
    size_t len;
    uint8_t data[DMX_BUF_SIZE];
} dmx_frame_t;

static TaskHandle_t tx_task = NULL, consumer_task = NULL;
static dmx_frame_t dmx_tx_buf;
static dmx_frame_t dmx_rx_buf;
static SemaphoreHandle_t tx_sem;
static SemaphoreHandle_t consumer_sem;
static QueueHandle_t uart_evt_queue;

static void dmx_consumer_task(void *arg)
{
    while (1) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            dmx_frame_t * frame = &dmx_rx_buf;
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
            xSemaphoreGive(consumer_sem);
        }
    }
}

/*
 * IMPORTANT NOTE ABOUT UART BREAK HANDLING ON ESP32 (ESP-IDF)
 *
 * On ESP32 UART driver, UART_BREAK event does NOT mean that all data
 * belonging to the previous DMX frame has already been delivered
 * via UART_DATA events.
 *
 * Observed behavior:
 *  - Incoming DMX data is delivered in chunks (~120 bytes).
 *  - When a BREAK occurs on the line, UART_BREAK event is generated
 *    with evt.size == 0.
 *  - At the moment UART_BREAK is received, uart_get_buffered_data_len()
 *    may report 0 bytes.
 *  - The LAST chunk of data (typically ~34 bytes) that was physically
 *    received BEFORE the BREAK is delivered *after* the UART_BREAK 
 *    event as a UART_DATA event.
 *  - Edge case:
 *    If (DMX frame length + 2) is exactly divisible by ~120 bytes,
 *    the final UART_DATA event is NOT generated at all.
 *    In this case, after UART_BREAK, the next UART_DATA contains bytes
 *    belonging to the *next* DMX frame.
 *
 * Because of this, UART_BREAK must be treated only as a synchronization
 * marker, not as a point where all frame data is already available.
 *
 * For this reason:
 *  - UART_DATA bytes are counted (to_read) but NOT read immediately.
 *  - Actual reading from the UART internal buffer happens only after
 *    UART_BREAK is received.
 *  - This guarantees that the full DMX frame is read contiguously,
 *    even though the last portion arrives after the BREAK event.
 */
static void dmx_rx_task(void *arg)
{
    uart_event_t evt;
    uint8_t write_idx = 0;
    bool is_sync = false;
    bool was_break = false;
    size_t to_read = 0;

    while (1) {
        if (!xQueueReceive(uart_evt_queue, &evt, portMAX_DELAY))
            continue;

        switch (evt.type) {

        case UART_DATA: {
            to_read += evt.size;
            if (to_read > DMX_BUF_SIZE) {
                was_break = false;
                is_sync = false;
                to_read = 0;
                uart_flush_input(DMX_UART_NUM);
                ESP_LOGI(TAG, "Too long packet");
            } else if (was_break) {
                // Read from buffer only after BREAK signal
                was_break = false;
                if (is_sync && xSemaphoreTake(consumer_sem, 0) == pdTRUE) {
                    size_t len = 0;
                    int n = 0;
                    while (to_read && (n = uart_read_bytes(DMX_UART_NUM, dmx_rx_buf.data + len, to_read, 0))) {
                        len += n;
                        to_read -= n;
                    }
                    dmx_rx_buf.len = len - 1;
                    xTaskNotifyGive(consumer_task);
                } else {
                    // Just remove bytes from internal buffer
                    static uint8_t dump[32];
                    while (to_read) {
                        to_read -= uart_read_bytes(DMX_UART_NUM, dump, MIN(to_read, sizeof(dump)), 0);
                    }
                    is_sync = true;
                }
            } else {
                // Keep data in UART internal buffer until BREAK detected
            }
            break;
        }

        case UART_BREAK: {
            // Next data frame will be last in this DMX packet
            was_break = true;
            break;
        }

        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            was_break = false;
            is_sync = false;
            to_read = 0;
            uart_flush_input(DMX_UART_NUM);
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
        // ESP_LOGE(TAG, "tx queue overflow");
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

    uart_driver_install(DMX_UART_NUM, 600, 0, 4, &uart_evt_queue, 0);
    uart_param_config(DMX_UART_NUM, &cfg);
    uart_set_pin(DMX_UART_NUM, DMX_TX_PIN, DMX_RX_PIN, DMX_RTS_PIN, UART_PIN_NO_CHANGE);

    tx_sem = xSemaphoreCreateBinary();
    consumer_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(tx_sem);
    xSemaphoreGive(consumer_sem);

    xTaskCreate(dmx_rx_task, "dmx_rx", 2048, NULL, 7, NULL);
    xTaskCreate(dmx_consumer_task, "dmx_consumer", 3072, NULL, 5, &consumer_task);
    xTaskCreate(dmx_tx_task, "dmx_tx", 2048, NULL, 5, &tx_task);

    return ESP_OK;
}
