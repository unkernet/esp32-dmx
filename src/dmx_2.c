#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "app_config.h"
#include "hardware_config.h"
#include "dmx_2.h"
#include "dmx_common.h"
#if defined(DMX_2_RX_PIN) && defined(DMX_2_TX_PIN)

#define DMX_2_UART_NUM      UART_NUM_0

static const char *TAG = "DMX_2";
static app_config_t *app_config;
static dmx_config *dmx_cfg;

void send_dmx_2_data(uint16_t universe, const uint8_t * data, uint16_t length)
{
    if (!app_config || universe != app_config->dmx_2_out_universe) {
        return;
    }

    send_dmx_data_common(dmx_cfg, data, length);
}

esp_err_t dmx_2_init(app_config_t *config)
{
    #ifdef DMX_2_EN
    gpio_set_direction(DMX_2_EN, GPIO_MODE_OUTPUT);
    #endif
    if ((config->enabled_modules & (MOD_EN_DMX_2_IN | MOD_EN_DMX_2_OUT)) == 0) {
        #ifdef DMX_2_EN
        gpio_set_level(DMX_2_EN, 0);
        #endif
        ESP_LOGI(TAG, "disabled");
        return ESP_OK;
    }
    
    dmx_cfg = malloc(sizeof(dmx_config));
    if (!dmx_cfg) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return ESP_ERR_NO_MEM;
    }
    memset(dmx_cfg, 0, sizeof(dmx_config));

    dmx_cfg->uart_num = DMX_2_UART_NUM;
    dmx_cfg->instance_name = TAG;
    dmx_cfg->source = DATA_SOURCE_DMX_2_IN;
    dmx_cfg->in_universe = config->dmx_2_in_universe;
    dmx_cfg->out_universe = config->dmx_2_out_universe;
    dmx_cfg->repeat_interval = config->dmx_2_repeat_interval;
    dmx_cfg->repeat_time = config->dmx_2_repeat_time;
    dmx_cfg->enabled = ((config->enabled_modules & MOD_EN_DMX_2_IN) ? MOD_EN_DMX_IN : 0) |
        ((config->enabled_modules & MOD_EN_DMX_2_OUT) ? MOD_EN_DMX_OUT : 0);

    RETURN_ON_ERROR(dmx_init_common(dmx_cfg, DMX_2_TX_PIN, DMX_2_RX_PIN));

    #ifdef DMX_2_EN
    gpio_set_level(DMX_2_EN, 1);
    #endif

    app_config = config;

    return ESP_OK;
}

#endif // defined(DMX_2_RX_PIN) && defined(DMX_2_TX_PIN)
