#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "dmx_common.h"
#include "hardware_config.h"
#include <string.h>
#include "router.h"

#define DMX_RTS_PIN       UART_PIN_NO_CHANGE
#define DMX_BREAK_BITS    23 // 23 * 4 us = 92 us
#define MIN_DMX_LEN       16
// #define DMX_BREAK_AFTER_SLOT // Comment this line to send Brake before slot

static void dmx_consumer_task(void *arg)
{
    dmx_config *cfg = (dmx_config*) arg;
    while (1) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            dmx_frame_t * frame = &cfg->dmx_rx_buf;
            if (frame->len >= MIN_DMX_LEN + 1 && frame->data[0] == 0) {
                size_t len = frame->len - 1;
                const uint8_t *data = frame->data + 1;
                uint16_t universe = cfg->in_universe;

                route_dmx_data(cfg->source, universe, data, len);
            }
            xSemaphoreGive(cfg->consumer_sem);
        }
    }
}

/*
 * IMPORTANT NOTE ABOUT UART / DMX FRAME HANDLING (PATCHED DRIVER)
 *
 * The standard ESP-IDF UART driver does not guarantee that all bytes
 * belonging to a DMX frame are delivered before the UART_BREAK event.
 * In practice, the last portion of the frame may be delivered *after*
 * BREAK, which makes it impossible to reliably use BREAK as a frame
 * boundary marker.
 *
 * To solve this, the UART driver (uart.c) has been patched:
 *
 *  - UART_BREAK is no longer delivered as a separate event.
 *  - Instead, BREAK is merged into the RX path.
 *  - A new event type UART_DATA_BREAK is introduced.
 *  - When a BREAK interrupt occurs, the driver:
 *      1. Reads all bytes currently available in the hardware FIFO.
 *      2. Emits a single event:
 *            type = UART_DATA_BREAK
 *            size = number of bytes read from FIFO
 *
 *  - If no data is present, size may be 0. In practice, the hardware
 *    often provides 1 extra byte (framing error / break symbol).
 *
 * Resulting event stream:
 *
 *   UART_DATA (size=N)
 *   UART_DATA (size=M)
 *   ...
 *   UART_DATA_BREAK (size=K)   <-- guaranteed frame boundary
 *
 * This guarantees that all bytes received *before BREAK* are delivered
 * before (or together with) the UART_DATA_BREAK event, eliminating the
 * ambiguity present in the original driver.
 */
static void dmx_rx_task(void *arg)
{
    dmx_config *cfg = (dmx_config*) arg;
    uart_event_t evt;
    bool is_sync = false;
    size_t to_read = 0;

    while (1) {
        if (!xQueueReceive(cfg->uart_evt_queue, &evt, portMAX_DELAY))
            continue;
        
        int64_t now = esp_timer_get_time();

        switch (evt.type) {

        case UART_DATA:
        case UART_DATA_BREAK: {
            to_read += evt.size;
            // ESP_LOGI("DEBUG", "DATA ev=%d to_read=%d is_sync=%d, br=%d", evt.size, to_read, is_sync, evt.type == UART_DATA_BREAK);
            if (to_read > DMX_BUF_SIZE) {
                is_sync = false;
                to_read = 0;
                uart_flush_input(cfg->uart_num);
                ESP_LOGI(cfg->instance_name, "Too long packet");
            } else if (evt.type == UART_DATA_BREAK) {
                // Read from buffer only after BREAK signal
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
                    uint8_t dump[32];
                    int n = 0;
                    while (to_read && (n = uart_read_bytes(cfg->uart_num, dump, MIN(to_read, sizeof(dump)), 0))) {
                        to_read -= n;
                    }
                    is_sync = true;
                }
            } else {
                // Keep data in UART internal buffer until BREAK detected
            }
            break;
        }

        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            is_sync = false;
            to_read = 0;
            uart_flush_input(cfg->uart_num);
            ESP_LOGE(cfg->instance_name, "UART buffer overflow");
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
    while (1) {
        bool should_transmit_now = false;
        BaseType_t notified = ulTaskNotifyTake(pdTRUE, cfg->dmx_data_was_sent ? max_frame_interval : portMAX_DELAY);

        if (notified > 0) {
            // Notified: new data is ready in dmx_tx_buf
            if (xSemaphoreTake(cfg->tx_sem, portMAX_DELAY) == pdTRUE) {
                if (cfg->dmx_tx_buf.len <= DMX_BUF_SIZE) {
                    cfg->tx_data_len_cache = cfg->dmx_tx_buf.len;
                    memcpy(cfg->tx_data_cache, cfg->dmx_tx_buf.data, cfg->dmx_tx_buf.len);
                    should_transmit_now = true;
                }
                xSemaphoreGive(cfg->tx_sem);
            }
        } else if (cfg->dmx_data_was_sent) {
            // Timeout: no new data. Re-send last frame if we have one.
            should_transmit_now = true;
        }

        if (should_transmit_now) {
            #ifdef DMX_BREAK_AFTER_SLOT
            // The DMX break occurs at the end of the frame
            uart_write_bytes_with_break(cfg->uart_num, cfg->tx_data_cache, cfg->tx_data_len_cache, DMX_BREAK_BITS);
            uart_wait_tx_done(cfg->uart_num, portMAX_DELAY);
            #else
            // The DMX break occurs at the beginning of the frame
            uart_set_line_inverse(cfg->uart_num, UART_SIGNAL_TXD_INV);
            esp_rom_delay_us(DMX_BREAK_BITS * 4); // Break
            uart_set_line_inverse(cfg->uart_num, UART_SIGNAL_INV_DISABLE);
            esp_rom_delay_us(8); // Mark After Break
            uart_write_bytes(cfg->uart_num, cfg->tx_data_cache, cfg->tx_data_len_cache);
            uart_wait_tx_done(cfg->uart_num, portMAX_DELAY);
            #endif

            vTaskDelay(MAX(1, pdMS_TO_TICKS(1))); // Mark Time After Slot, 1ms
        }
    }
}

void send_dmx_data_common(dmx_config *cfg, const uint8_t * data, uint16_t length)
{
    if (length > DMX_BUF_SIZE - 2) {
        length = DMX_BUF_SIZE - 2;
    }

    if (xSemaphoreTake(cfg->tx_sem, 0) != pdTRUE) {
        // ESP_LOGE(cfg->instance_name, "tx queue overflow");
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

    uart_driver_install(cfg->uart_num, 600, 0, 4, &cfg->uart_evt_queue, ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3);
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
        ESP_LOGI(cfg->instance_name, "rx disabled");
    }
    if ((cfg->enabled & 2) != 0) {
        if (cfg->repeat_interval < 1) {
            cfg->repeat_interval = 1;
        }
        xTaskCreate(dmx_tx_task, "dmx_tx", 2048, cfg, 5, &cfg->tx_task);
    } else {
        ESP_LOGI(cfg->instance_name, "tx disabled");
    }
    return;
}
