#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "modules.h"
#include "dmx.h"
#include "router.h"

#define DMX_BREAK_BITS    23 // 23 * 4 us = 92 us
#define MIN_DMX_LEN       16
#define DMX_BUF_SIZE      (DMX_LEN + 2)
#define REPEAT_TIME_ENDLESS (0xff)

typedef struct {
    size_t len;
    uint8_t data[DMX_BUF_SIZE];
} dmx_frame_t;

typedef struct {
    uint8_t uart_num;
    uint16_t in_universe;
    uint16_t out_universe;
    uint16_t repeat_interval;
    uint8_t repeat_time;
    uint32_t enabled_mask;
    dmx_data_source_t source;
    const char *instance_name;
    TaskHandle_t tx_task;
    dmx_frame_t dmx_tx_buf;
    SemaphoreHandle_t tx_sem;
    QueueHandle_t uart_evt_queue;
    int8_t en_pin;
} dmx_port_t;

static const char *TAG = "DMX";
static dmx_port_t *registered_ports[UART_NUM_MAX] = {0};

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
    dmx_port_t *port = (dmx_port_t*) arg;
    uart_event_t evt;
    bool is_sync = false;
    size_t to_read = 0;

    while (1) {
        if (!xQueueReceive(port->uart_evt_queue, &evt, portMAX_DELAY))
            continue;
        
        switch (evt.type) {
        case UART_DATA:
        case UART_DATA_BREAK: {
            to_read += evt.size;
            if (to_read > DMX_BUF_SIZE) {
                ESP_LOGD(port->instance_name, "Too long packet: %d", to_read);
                is_sync = false;
                to_read = 0;
                uart_flush_input(port->uart_num);
            } else if (evt.type == UART_DATA_BREAK) {
                // Read from buffer only after BREAK signal
                if (is_sync) {
                    int n = 0;
                    dmx_frame_t frame;
                    frame.len = 0;
                    while (to_read > 0 && (n = uart_read_bytes(port->uart_num, frame.data + frame.len, to_read, 0)) > 0) {
                        frame.len += n;
                        to_read -= n;
                    }
                    is_sync = (to_read == 0);
                    if (is_sync && frame.len >= MIN_DMX_LEN + 2 && frame.data[0] == 0) {
                        // Remove DMX Start code and Break byte
                        route_dmx_data(port->source, port->in_universe, frame.data + 1, frame.len - 2);
                    }
                } else {
                    // Just remove bytes from internal buffer
                    uint8_t dump[120]; // UART_FULL_THRESH_DEFAULT
                    int n = 0;
                    while (to_read > 0 && (n = uart_read_bytes(port->uart_num, dump, MIN(to_read, sizeof(dump)), 0)) > 0) {
                        to_read -= n;
                    }
                    is_sync = (to_read == 0);
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
            uart_flush_input(port->uart_num);
            ESP_LOGD(port->instance_name, "UART buffer overflow");
            break;
        default:
            break;
        }
    }
}

static void dmx_tx_task(void *arg)
{
    dmx_port_t *port = (dmx_port_t*) arg;
    TickType_t max_frame_interval = pdMS_TO_TICKS(MAX(1, port->repeat_interval) - 1);
    // If there was no new data for a `repeat_time` second, last packet retransmission will stop
    int64_t end_time_us = 0;
    bool active = false;
    dmx_frame_t frame;

    while (1) {
        BaseType_t notified = ulTaskNotifyTake(pdTRUE, active ? max_frame_interval : portMAX_DELAY);

        if (notified > 0) {
            // Notified: new data is ready in dmx_tx_buf
            if (xSemaphoreTake(port->tx_sem, portMAX_DELAY) == pdTRUE) {
                frame.len = port->dmx_tx_buf.len + 1;
                frame.data[0] = 0; // Start byte
                memcpy(frame.data + 1, port->dmx_tx_buf.data, port->dmx_tx_buf.len);
                xSemaphoreGive(port->tx_sem);
                active = true;
                if (port->repeat_time != REPEAT_TIME_ENDLESS) {
                    end_time_us = esp_timer_get_time() + (int64_t)port->repeat_time * 1000000;
                }
            }
        }

        if (active) {
            #if (DMX_BREAK_AFTER_SLOT)
                // The DMX break occurs at the end of the frame
                uart_write_bytes_with_break(port->uart_num, frame.data, frame.len, DMX_BREAK_BITS);
                uart_wait_tx_done(port->uart_num, portMAX_DELAY);
            #else
                // The DMX break occurs at the beginning of the frame
                uart_set_line_inverse(port->uart_num, UART_SIGNAL_TXD_INV);
                esp_rom_delay_us(DMX_BREAK_BITS * 4);
                uart_set_line_inverse(port->uart_num, UART_SIGNAL_INV_DISABLE);
                esp_rom_delay_us(8);
                uart_write_bytes(port->uart_num, frame.data, frame.len);
                uart_wait_tx_done(port->uart_num, portMAX_DELAY);
            #endif

            vTaskDelay(MAX(1, pdMS_TO_TICKS(1)));

            if (port->repeat_time != REPEAT_TIME_ENDLESS && esp_timer_get_time() > end_time_us) {
                active = false;
            }
        }
    }
}

static esp_err_t dmx_init_port(const dmx_port_settings_t *settings, int8_t uart_num, int8_t tx_pin, int8_t rx_pin, int8_t en_pin, dmx_data_source_t source, uint32_t enabled_mask, const char *instance_name)
{
    if (tx_pin < 0 && rx_pin < 0) return ESP_OK;

    if (en_pin >= 0) {
        gpio_set_direction(en_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(en_pin, 0);
    }

    if (uart_num < 0 || uart_num >= UART_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    dmx_port_t *port = calloc(1, sizeof(dmx_port_t));
    RETURN_ON_NULL(port, ESP_ERR_NO_MEM);

    port->uart_num = uart_num;
    port->in_universe = settings->in_universe;
    port->out_universe = settings->out_universe;
    port->repeat_interval = settings->repeat_interval;
    port->repeat_time = settings->repeat_time;
    port->enabled_mask = enabled_mask;
    port->source = source;
    port->instance_name = instance_name;
    port->en_pin = en_pin;

    uart_config_t uart_cfg = {
        .baud_rate  = 250000,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_2,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    RETURN_ON_ERROR(uart_driver_install(port->uart_num, DMX_BUF_SIZE + 120, 0, 4, &port->uart_evt_queue, ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3));
    RETURN_ON_ERROR(uart_param_config(port->uart_num, &uart_cfg));
    RETURN_ON_ERROR(uart_set_pin(port->uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    RETURN_ON_NULL(port->tx_sem = xSemaphoreCreateBinary(), ESP_ERR_NO_MEM);
    xSemaphoreGive(port->tx_sem);

    if ((enabled_mask & (MOD_EN_DMX_0_IN | MOD_EN_DMX_1_IN | MOD_EN_DMX_2_IN | MOD_EN_DMX_3_IN)) && rx_pin >= 0) {
        TaskHandle_t dmx_rx_task_handle;
        xTaskCreate(dmx_rx_task, "dmx_rx", 2304, port, 7, &dmx_rx_task_handle);
        RETURN_ON_NULL(dmx_rx_task_handle, ESP_ERR_NO_MEM);
    } else {
        ESP_LOGI(instance_name, "rx disabled");
    }

    if ((enabled_mask & (MOD_EN_DMX_0_OUT | MOD_EN_DMX_1_OUT | MOD_EN_DMX_2_OUT | MOD_EN_DMX_3_OUT)) && tx_pin >= 0) {
        xTaskCreate(dmx_tx_task, "dmx_tx", 2048, port, 5, &port->tx_task);
        RETURN_ON_NULL(port->tx_task, ESP_ERR_NO_MEM);
    } else {
        ESP_LOGI(instance_name, "tx disabled");
    }

    if (en_pin >= 0) {
        gpio_set_level(en_pin, 1);
    }

    registered_ports[port->uart_num] = port;
    ESP_LOGI(TAG, "Registered %s on UART %d", instance_name, port->uart_num);

    return ESP_OK;
}

void dmx_send(uint16_t universe, const uint8_t *data, uint16_t length)
{
    if (length > DMX_LEN) length = DMX_LEN;

    for (int i = 0; i < UART_NUM_MAX; i++) {
        dmx_port_t *port = registered_ports[i];
        if (port && (port->enabled_mask & (MOD_EN_DMX_0_OUT | MOD_EN_DMX_1_OUT | MOD_EN_DMX_2_OUT | MOD_EN_DMX_3_OUT)) && port->out_universe == universe) {
            if (xSemaphoreTake(port->tx_sem, 0) == pdTRUE) {
                port->dmx_tx_buf.len = length;
                memcpy(port->dmx_tx_buf.data, data, length);
                xSemaphoreGive(port->tx_sem);
                xTaskNotifyGive(port->tx_task);
            }
        }
    }
}

esp_err_t dmx_init(app_config_t *config)
{

    #ifdef _DMX_0_EN
    #if DMX_0_UART < 0 || DMX_0_UART >= SOC_UART_NUM
    #error "Invalid UART port number"
    #endif
    RETURN_ON_ERROR(dmx_init_port(&config->dmx_ports[0], DMX_0_UART, DMX_0_TX_PIN, DMX_0_RX_PIN, DMX_0_EN_PIN, DATA_SOURCE_DMX_0_IN, 
        config->enabled_modules & (MOD_EN_DMX_0_IN | MOD_EN_DMX_0_OUT), DMX_0_NAME));
    #endif

    #ifdef _DMX_1_EN
    #if DMX_1_UART < 0 || DMX_1_UART >= SOC_UART_NUM
    #error "Invalid UART port number"
    #endif
    RETURN_ON_ERROR(dmx_init_port(&config->dmx_ports[1], DMX_1_UART, DMX_1_TX_PIN, DMX_1_RX_PIN, DMX_1_EN_PIN, DATA_SOURCE_DMX_1_IN, 
        config->enabled_modules & (MOD_EN_DMX_1_IN | MOD_EN_DMX_1_OUT), DMX_1_NAME));
    #endif

    #ifdef _DMX_2_EN
    #if DMX_2_UART < 0 || DMX_2_UART >= SOC_UART_NUM
    #error "Invalid UART port number"
    #endif
    RETURN_ON_ERROR(dmx_init_port(&config->dmx_ports[2], DMX_2_UART, DMX_2_TX_PIN, DMX_2_RX_PIN, DMX_2_EN_PIN, DATA_SOURCE_DMX_2_IN, 
        config->enabled_modules & (MOD_EN_DMX_2_IN | MOD_EN_DMX_2_OUT), DMX_2_NAME));
    #endif

    #ifdef _DMX_3_EN
    #if DMX_3_UART < 0 || DMX_3_UART >= SOC_UART_NUM
    #error "Invalid UART port number"
    #endif
    RETURN_ON_ERROR(dmx_init_port(&config->dmx_ports[3], DMX_3_UART, DMX_3_TX_PIN, DMX_3_RX_PIN, DMX_3_EN_PIN, DATA_SOURCE_DMX_3_IN, 
        config->enabled_modules & (MOD_EN_DMX_3_IN | MOD_EN_DMX_3_OUT), DMX_3_NAME));
    #endif

    return ESP_OK;
}
