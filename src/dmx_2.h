#ifndef DMX_2_H
#define DMX_2_H

#include "esp_err.h"
#include "app_config.h"

void send_dmx_2_data(uint16_t universe, const uint8_t * data, uint16_t length);
esp_err_t dmx_2_init(app_config_t *config);

#endif // DMX_2_H
