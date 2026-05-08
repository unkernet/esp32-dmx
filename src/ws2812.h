#ifndef WS2812_H
#define WS2812_H

#include "esp_err.h"
#include "app_config.h"

/**
 * @brief Send RGB(W) data to WS2812 strip.
 * @param universe The target DMX universe.
 * @param data Pointer to the pixel payload.
 * @param length Length of the payload.
 */
void send_ws2812_data(uint16_t universe, const uint8_t *data, uint16_t length);

/**
 * @brief Initialize WS2812 hardware interface.
 * @param config Pointer to the application configuration.
 * @return esp_err_t ESP_OK on success, or an error code.
 */
esp_err_t ws2812_init(app_config_t *config);

#endif // WS2812_H
