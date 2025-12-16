#ifndef DMX_H
#define DMX_H

#include "esp_err.h"
#include "app_config.h"

void send_dmx_data(uint8_t universe, const uint8_t * data, uint16_t length);
esp_err_t dmx_init(app_config_t *config);

#endif // DMX_H
