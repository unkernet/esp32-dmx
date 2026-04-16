#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "dmx_common.h"
#include "hardware_config.h"
#include <string.h>
#include "router.h"

#define DMX_RTS_PIN       UART_PIN_NO_CHANGE
#define DMX_BREAK_BITS    23 // 23 * 4 us = 92 us
#define MIN_DMX_LEN       16

static const char *TAG = "DMX_COMMON";

static void dmx_consumer_task(void *arg)
{
    dmx_config *cfg = (dmx_config*) arg;
    while (1) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            dmx_frame_t * frame = &cfg->dmx_rx_buf;
            if (frame->len >= MIN_DMX_LEN + 1 && frame->data[0] == 0) {
                size_t len = frame->len - 1;
                const uint8_t *data = frame->data + 1;
                uint8_t universe = cfg->in_universe;

                route_dmx_data(DATA_SOURCE_DMX_IN, universe, data, len);
            }
            xSemaphoreGive(cfg->consumer_sem);
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
    dmx_config *cfg = (dmx_config*) arg;
    uart_event_t evt;
    bool is_sync = false;
    bool was_break = false;
    size_t to_read = 0;

    while (1) {
        if (!xQueueReceive(cfg->uart_evt_queue, &evt, portMAX_DELAY))
            continue;

        switch (evt.type) {

        case UART_DATA: {
            to_read += evt.size;
            if (to_read > DMX_BUF_SIZE) {
                was_break = false;
                is_sync = false;
                to_read = 0;
                uart_flush_input(cfg->uart_num);
                ESP_LOGI(TAG, "Too long packet");
            } else if (was_break) {
                // Read from buffer only after BREAK signal
                was_break = false;
                if (is_sync && xSemaphoreTake(cfg->consumer_sem, 0) == pdTRUE) {
                    size_t len = 0;
                    int n = 0;
                    while (to_read && (n = uart_read_bytes(cfg->uart_num, cfg->dmx_rx_buf.data + len, to_read, 0))) {
                        len += n;
                        to_read -= n;
                    }
                    cfg->dmx_rx_buf.len = len - 1;
                    xTaskNotifyGive(cfg->consumer_task);
                } else {
                    // Just remove bytes from internal buffer
                    static uint8_t dump[32];
                    while (to_read) {
                        to_read -= uart_read_bytes(cfg->uart_num, dump, MIN(to_read, sizeof(dump)), 0);
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
            uart_flush_input(cfg->uart_num);
            ESP_LOGE(TAG, "UART buffer overflow");
            break;

        default:
            break;
        }
    }
}

static void dmx_tx_task(void *arg)
{
    dmx_config *cfg = (dmx_config*) arg;
    TickType_t max_frame_interval = pdMS_TO_TICKS(MAX(0, cfg->repeat_interval * 5 - 1));
    static uint8_t tx_data[DMX_BUF_SIZE];
    static size_t tx_data_len;
    while (1) {
        bool should_transmit_now = false;
        BaseType_t notified = ulTaskNotifyTake(pdTRUE, cfg->dmx_data_was_sent ? max_frame_interval : portMAX_DELAY);

        if (notified > 0) {
            // Notified: new data is ready in dmx_tx_buf
            if (xSemaphoreTake(cfg->tx_sem, portMAX_DELAY) == pdTRUE) {
                if (cfg->dmx_tx_buf.len <= DMX_BUF_SIZE) {
                    tx_data_len = cfg->dmx_tx_buf.len;
                    memcpy(tx_data, cfg->dmx_tx_buf.data, cfg->dmx_tx_buf.len);
                    should_transmit_now = true;
                }
                xSemaphoreGive(cfg->tx_sem);
            }
        } else if (cfg->dmx_data_was_sent) {
            // Timeout: no new data. Re-send last frame if we have one.
            should_transmit_now = true;
        }

        if (should_transmit_now) {
            // The DMX break occurs at the beginning of the frame
            uart_set_line_inverse(cfg->uart_num, UART_SIGNAL_TXD_INV);
            esp_rom_delay_us(92); // Break
            uart_set_line_inverse(cfg->uart_num, UART_SIGNAL_INV_DISABLE);
            esp_rom_delay_us(8); // Mark After Break
            uart_write_bytes(cfg->uart_num, tx_data, tx_data_len);
            uart_wait_tx_done(cfg->uart_num, portMAX_DELAY);
            vTaskDelay(MAX(1, pdMS_TO_TICKS(1))); // Mark Time After Slot, 1ms
        }
    }
}

void send_dmx_data_common(dmx_config *cfg, const uint8_t * data, uint16_t length)
{
    if (length > DMX_BUF_SIZE - 2) {
        return;
    }

    if (xSemaphoreTake(cfg->tx_sem, 0) != pdTRUE) {
        // ESP_LOGE(TAG, "tx queue overflow");
        return;
    }

    cfg->dmx_tx_buf.len = length + 1;
    cfg->dmx_tx_buf.data[0] = 0;
    memcpy(cfg->dmx_tx_buf.data + 1, data, length);
    cfg->dmx_data_was_sent = true;

    xSemaphoreGive(cfg->tx_sem);
    xTaskNotifyGive(cfg->tx_task);
}

void dmx_init_common(dmx_config *cfg, uint8_t tx_pin,  uint8_t rx_pin)
{
    uart_config_t uart_cfg = {
        .baud_rate  = 250000,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_2,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(cfg->uart_num, 600, 0, 4, &cfg->uart_evt_queue, 0);
    uart_param_config(cfg->uart_num, &uart_cfg);
    uart_set_pin(cfg->uart_num, tx_pin, rx_pin, DMX_RTS_PIN, UART_PIN_NO_CHANGE);

    cfg->tx_sem = xSemaphoreCreateBinary();
    cfg->consumer_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(cfg->tx_sem);
    xSemaphoreGive(cfg->consumer_sem);

    if ((cfg->enabled & 1) != 0) {
        xTaskCreate(dmx_rx_task, "dmx_rx", 2048, cfg, 7, NULL);
        xTaskCreate(dmx_consumer_task, "dmx_consumer", 3072, cfg, 5, &cfg->consumer_task);
    } else {
        ESP_LOGI(TAG, "rx disabled");
    }
    if ((cfg->enabled & 2) != 0) {
        if (cfg->repeat_interval < 1) {
            cfg->repeat_interval = 1;
        }
        xTaskCreate(dmx_tx_task, "dmx_tx", 2048, cfg, 5, &cfg->tx_task);
    } else {
        ESP_LOGI(TAG, "tx disabled");
    }
    return;
}
