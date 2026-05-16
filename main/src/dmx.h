#ifndef DMX_H
#define DMX_H

#include "esp_err.h"
#include "app_config.h"

/**
 * @brief Send DMX data on Port 1.
 * @param universe The target DMX universe.
 * @param data Pointer to the DMX payload.
 * @param length Length of the payload.
 */
void send_dmx_data(uint16_t universe, const uint8_t *data, uint16_t length);

/**
 * @brief Initialize DMX Port 1.
 * @param config Pointer to the application configuration.
 * @return esp_err_t ESP_OK on success, or an error code.
 */
esp_err_t dmx_init(app_config_t *config);

#endif // DMX_H
