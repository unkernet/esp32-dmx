#ifndef WS2812_h
#define WS2812_h

#include "esp_err.h"
#include "app_config.h"

void send_ws2812_data(uint16_t universe, const uint8_t * data, uint16_t length);
esp_err_t ws2812_init(app_config_t *config);

#endif // WS2812_h
