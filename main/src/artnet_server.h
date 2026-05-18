#ifndef ARTNET_SERVER_H
#define ARTNET_SERVER_H

#include "app_config.h"
#include "esp_err.h"
#include "router.h"

/**
 * @brief Start the Art-Net server task.
 * @param config Pointer to the application configuration.
 * @return esp_err_t ESP_OK on success, or an error code.
 */
esp_err_t start_artnet_server(app_config_t *config);

/**
 * @brief Send DMX data via Art-Net to the network.
 * @param universe The target DMX universe.
 * @param data Pointer to the DMX payload.
 * @param length Length of the payload.
 */
void send_artnet_dmx_data(uint16_t universe, const uint8_t *data, uint16_t length, dmx_data_source_t source);

#endif // ARTNET_SERVER_H
