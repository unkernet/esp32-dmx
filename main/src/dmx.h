#ifndef DMX_H
#define DMX_H

#include "esp_err.h"
#include "app_config.h"
#include "router.h"

/**
 * @brief Send DMX data to any port registered with the matching universe.
 * @param universe The target DMX universe.
 * @param data Pointer to the DMX payload.
 * @param length Length of the payload.
 */
void dmx_send(uint16_t universe, const uint8_t *data, uint16_t length);

/**
 * @brief Initialize DMX ports based on configuration.
 * @param config Pointer to the application configuration.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t dmx_init(app_config_t *config);

#endif // DMX_H
