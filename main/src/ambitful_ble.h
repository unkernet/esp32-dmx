#ifndef AMBITFUL_BLE_H
#define AMBITFUL_BLE_H

#include "esp_err.h"
#include "app_config.h"

/**
 * @brief Initialize BLE beacon advertising for Ambitful lights.
 * @param config Pointer to the application configuration.
 * @return esp_err_t ESP_OK on success, or an error code.
 */
esp_err_t ambitful_ble_init(app_config_t *config);

/**
 * @brief Queue DMX data for BLE beacon advertisement.
 * @param universe The target DMX universe.
 * @param data Pointer to the DMX payload.
 * @param length Length of the payload.
 */
void send_ambitful_dmx_data(uint16_t universe, const uint8_t *data, uint16_t length);

#endif // AMBITFUL_BLE_H
