#ifndef ARTNET_SERVER_H
#define ARTNET_SERVER_H

#include "app_config.h"
#include "esp_err.h"

esp_err_t start_artnet_server(app_config_t *config);
void send_artnet_dmx_data(uint16_t universe, const uint8_t * data, uint16_t length, uint8_t seq);

#endif // ARTNET_SERVER_H
