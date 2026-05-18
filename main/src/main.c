#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_pm.h"
#include "driver/gpio.h"
#include "modules.h"
#include "wifi_manager.h"
#include "web_server.h"
#ifdef ARTNET
#include "artnet_server.h"
#endif
#ifdef AMBITFUL_BLE
#include "ambitful_ble.h"
#endif
#ifdef _WS2812_EN
#include "ws2812.h"
#endif
#ifdef _DMX_EN
#include "dmx.h"
#endif
#include "app_config.h"
#include "app_config_nvs.h"
#include "dns.h"
#ifdef LUA_INTERPRETER
#include "lua_interpreter.h"
#endif

static const char *TAG = "MAIN";

app_config_t app_config;

void app_main() {
    #if CONFIG_PM_ENABLE
        esp_pm_config_t pm_config = {
            .max_freq_mhz = 160,
            .min_freq_mhz = 80,
            .light_sleep_enable = false
        };
        ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
    #endif

    // Initialize NVS
    ESP_ERROR_CHECK(app_config_nvs_init());

    // Load application configuration
    esp_err_t err = app_config_load(&app_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load configuration from NVS (%s), using default values.", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Configuration loaded successfully from NVS.");
    }

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };
    
    err = esp_vfs_spiffs_register(&conf);

    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(err));
        }
        return;
    }

    LOG_ON_ERROR(wifi_manager_init(&app_config), TAG, "wifi_manager_init failed");
    #ifdef AMBITFUL_BLE
    LOG_ON_ERROR(ambitful_ble_init(&app_config), TAG, "ambitful_ble_init failed");
    #endif
    #ifdef _DMX_EN
    LOG_ON_ERROR(dmx_init(&app_config), TAG, "dmx_init failed");
    #endif
    #ifdef _WS2812_EN
    LOG_ON_ERROR(ws2812_init(&app_config), TAG, "ws2812_init failed");
    #endif
    LOG_ON_ERROR(start_webserver(&app_config), TAG, "start_webserver failed");
    #ifdef ARTNET
    LOG_ON_ERROR(start_artnet_server(&app_config), TAG, "start_artnet_server failed");
    #endif
    LOG_ON_ERROR(start_mdns(), TAG, "start_mdns failed");
    #ifdef LUA_INTERPRETER
    LOG_ON_ERROR(lua_interpreter_init(), TAG, "lua_interpreter_init failed");
    #endif
}
