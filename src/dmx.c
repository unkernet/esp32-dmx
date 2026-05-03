#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "hardware_config.h"
#include "dmx.h"
#include "dmx_common.h"
#include "router.h"

#define DMX_UART_NUM      UART_NUM_1

static const char *TAG = "DMX";
static app_config_t *app_config;
static dmx_config dmx_cfg;

void send_dmx_data(uint16_t universe, const uint8_t * data, uint16_t length)
{
    if (!app_config || universe != app_config->dmx_out_universe) {
        return;
    }

    send_dmx_data_common(&dmx_cfg, data, length);
}

esp_err_t dmx_init(app_config_t *config)
{
    #ifdef DMX_EN
    gpio_set_direction(DMX_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(DMX_EN, 0);
    #endif
    if ((config->enabled_modules & (MOD_EN_DMX_IN | MOD_EN_DMX_OUT)) == 0) {
        ESP_LOGI(TAG, "disabled");
        return ESP_OK;
    }
    app_config = config;

    dmx_cfg.uart_num = DMX_UART_NUM;
    dmx_cfg.instance_name = TAG;
    dmx_cfg.source = DATA_SOURCE_DMX_1_IN;
    dmx_cfg.in_universe = config->dmx_in_universe;
    dmx_cfg.out_universe = config->dmx_out_universe;
    dmx_cfg.repeat_interval = config->dmx_repeat_interval;
    dmx_cfg.enabled = ((app_config->enabled_modules & MOD_EN_DMX_IN) ? 1 : 0) +
        ((app_config->enabled_modules & MOD_EN_DMX_OUT) ? 2 : 0);

    #ifdef DMX_EN
    gpio_set_level(DMX_EN, 1);
    #endif
    dmx_init_common(&dmx_cfg, DMX_TX_PIN, DMX_RX_PIN);

    return ESP_OK;
}
