#ifndef BLE_BEACON_H
#define BLE_BEACON_H

#include "esp_err.h"

esp_err_t ble_beacon_init(void);
esp_err_t ble_beacon_set_data(const uint8_t* data, uint8_t data_len);
void ble_beacon_task(void *param);

#endif // BLE_BEACON_H
