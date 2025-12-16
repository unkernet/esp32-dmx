#include "esp_err.h"
#include "ws2812.h"

static app_config_t *app_config;

void send_ws2812_data(uint8_t universe, const uint8_t * data, uint16_t length) {
    if (!app_config || app_config->ws2812_universe != universe) {
        return;
    }
    // Send data
}

esp_err_t ws2812_init(app_config_t *config) {
    app_config = config;
    return ESP_OK;
}
