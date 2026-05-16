#ifndef DMX_COMMON_H
#define DMX_COMMON_H

#include <stdint.h>
#include "freertos/task.h"
#include "freertos/queue.h"
#include "router.h"
#include "modules.h"

/** @brief DMX buffer size (start code + DMX_LEN + break) */
#define DMX_BUF_SIZE (DMX_LEN + 2) 

/**
 * @brief DMX frame structure.
 */
typedef struct {
    size_t len;                  ///< Length of data in the frame
    uint8_t data[DMX_BUF_SIZE];  ///< DMX payload buffer
} dmx_frame_t;

/**
 * @brief Configuration structure for a DMX port.
 */
typedef struct {
    uint8_t uart_num;            ///< UART port number
    uint16_t in_universe;        ///< Input universe mapping
    uint16_t out_universe;       ///< Output universe mapping
    uint8_t repeat_interval;     ///< Transmission repeat interval
    uint8_t repeat_time;         ///< Transmission repeat timeout
    uint8_t enabled;             ///< Module enable flag
    dmx_data_source_t source;    ///< Data source enum
    const char *instance_name;   ///< Debug instance name
    TaskHandle_t tx_task;        ///< Handle for transmit task
    dmx_frame_t dmx_tx_buf;      ///< Transmit buffer
    SemaphoreHandle_t tx_sem;    ///< Transmit mutex/semaphore
    QueueHandle_t uart_evt_queue;///< UART event queue
} dmx_config;

/**
 * @brief Initialize common DMX hardware and tasks.
 * @param cfg Pointer to the DMX configuration structure.
 * @param tx_pin TX GPIO pin.
 * @param rx_pin RX GPIO pin.
 */
esp_err_t dmx_init_common(dmx_config *cfg, int8_t tx_pin, int8_t rx_pin);

/**
 * @brief Send DMX data on the specified port.
 * @param cfg Pointer to the DMX configuration.
 * @param data Pointer to the data array.
 * @param length Length of data in bytes.
 */
void send_dmx_data_common(dmx_config *cfg, const uint8_t *data, uint16_t length);

#endif // DMX_COMMON_H
