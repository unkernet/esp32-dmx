#ifndef DMX_COMMON_H
#define DMX_COMMON_H

#include <stdint.h>
#include "freertos/task.h"
#include "freertos/queue.h"

#include "router.h"

/*
 * On ESP32 UART (ESP-IDF), one extra zero byte is consistently observed
 * at the end of each frame when using UART_BREAK detection.
 * This byte is associated with the BREAK condition and is discarded
 * during processing.
 */
#define DMX_BUF_SIZE      514 // start code + 512 + break

typedef struct {
    size_t len;
    uint8_t data[DMX_BUF_SIZE];
} dmx_frame_t;

typedef struct {
    uint8_t uart_num;
    uint16_t in_universe;
    uint16_t out_universe;
    uint8_t repeat_interval;
    uint8_t enabled;
    dmx_data_source_t source;
    const char *instance_name;
    TaskHandle_t tx_task;
    TaskHandle_t consumer_task;
    dmx_frame_t dmx_tx_buf;
    dmx_frame_t dmx_rx_buf;
    uint8_t tx_data_cache[DMX_BUF_SIZE];
    size_t tx_data_len_cache;
    SemaphoreHandle_t tx_sem;
    SemaphoreHandle_t consumer_sem;
    QueueHandle_t uart_evt_queue;
    bool dmx_data_was_sent;
} dmx_config;

void dmx_init_common(dmx_config *cfg, uint8_t tx_pin,  uint8_t rx_pin);
void send_dmx_data_common(dmx_config *cfg, const uint8_t * data, uint16_t length);

#endif // DMX_COMMON_H
