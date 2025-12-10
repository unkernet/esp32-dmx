#ifndef AMBITFUL_BLE_H
#define AMBITFUL_BLE_H

#include "esp_err.h"
#include "app_config.h"

esp_err_t ambitful_ble_init(app_config_t *config);
void send_ambitful_dmx_data(uint8_t universe, const uint8_t * data, uint16_t length);

#endif // AMBITFUL_BLE_H
