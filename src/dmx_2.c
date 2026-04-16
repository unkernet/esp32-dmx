#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "hardware_config.h"
#include "dmx_2.h"
#include "dmx_common.h"
#if defined(DMX_2_RX_PIN) && defined(DMX_2_TX_PIN)

#define DMX_2_UART_NUM      UART_NUM_0

static const char *TAG = "DMX_2";
static app_config_t *app_config;
static dmx_config dmx_cfg;

void send_dmx_2_data(uint16_t universe, const uint8_t * data, uint16_t length)
{
    if (!app_config || universe != app_config->dmx_out_universe) {
        return;
    }

    send_dmx_data_common(&dmx_cfg, data, length);
}

esp_err_t dmx_2_init(app_config_t *config)
{
    if ((config->enabled_modules & (MOD_EN_DMX_2_IN | MOD_EN_DMX_2_OUT)) == 0) {
        ESP_LOGI(TAG, "1 disabled");
        return ESP_OK;
    }
    app_config = config;

    dmx_cfg.uart_num = DMX_2_UART_NUM;
    dmx_cfg.in_universe = config->dmx_in_universe;
    dmx_cfg.out_universe = config->dmx_out_universe;
    dmx_cfg.repeat_interval = config->dmx_repeat_interval;
    dmx_cfg.enabled = ((app_config->enabled_modules & MOD_EN_DMX_2_IN) ? 1 : 0) +
        ((app_config->enabled_modules & MOD_EN_DMX_2_OUT) ? 2 : 0);

    dmx_init_common(&dmx_cfg, DMX_2_TX_PIN, DMX_2_RX_PIN);

    return ESP_OK;
}

#endif // defined(DMX_2_RX_PIN) && defined(DMX_2_TX_PIN)
