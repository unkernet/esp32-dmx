#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "modules.h"
#include "dmx.h"
#include "dmx_common.h"
#include "router.h"
#ifdef _DMX_EN

#define DMX_UART_NUM UART_NUM_1
#if defined(DMX_EN_PIN) && DMX_EN_PIN < 0
#undef DMX_EN_PIN
#endif
#ifndef DMX_RX_PIN
#define DMX_RX_PIN (-1)
#endif
#ifndef DMX_TX_PIN
#define DMX_TX_PIN (-1)
#endif

static const char *TAG = "DMX";
static app_config_t *app_config;
static dmx_config *dmx_cfg;

void send_dmx_data(uint16_t universe, const uint8_t * data, uint16_t length)
{
    if (!app_config || universe != app_config->dmx_out_universe) {
        return;
    }

    send_dmx_data_common(dmx_cfg, data, length);
}

esp_err_t dmx_init(app_config_t *config)
{
    #ifdef DMX_EN_PIN
    gpio_set_direction(DMX_EN_PIN, GPIO_MODE_OUTPUT);
    #endif
    if ((config->enabled_modules & (MOD_EN_DMX_IN | MOD_EN_DMX_OUT)) == 0) {
        #ifdef DMX_EN_PIN
        gpio_set_level(DMX_EN_PIN, 0);
        #endif
        ESP_LOGI(TAG, "disabled");
        return ESP_OK;
    }

    RETURN_ON_NULL(dmx_cfg = malloc(sizeof(dmx_config)), ESP_ERR_NO_MEM);
    memset(dmx_cfg, 0, sizeof(dmx_config));

    dmx_cfg->uart_num = DMX_UART_NUM;
    dmx_cfg->instance_name = TAG;
    dmx_cfg->source = DATA_SOURCE_DMX_1_IN;
    dmx_cfg->in_universe = config->dmx_in_universe;
    dmx_cfg->out_universe = config->dmx_out_universe;
    dmx_cfg->repeat_interval = config->dmx_repeat_interval;
    dmx_cfg->repeat_time = config->dmx_repeat_time;
    dmx_cfg->enabled = ((config->enabled_modules & MOD_EN_DMX_IN) ? MOD_EN_DMX_IN : 0) |
        ((config->enabled_modules & MOD_EN_DMX_OUT) ? MOD_EN_DMX_OUT : 0);

    RETURN_ON_ERROR(dmx_init_common(dmx_cfg, DMX_TX_PIN, DMX_RX_PIN));

    #ifdef DMX_EN_PIN
    gpio_set_level(DMX_EN_PIN, 1);
    #endif

    app_config = config;

    return ESP_OK;
}
#endif // _DMX_EN
