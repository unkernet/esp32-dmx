#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_pm.h"
#include "driver/gpio.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "artnet_server.h"
#include "ambitful_ble.h"
#include "app_config.h"
#include "app_config_nvs.h"

#define BLINK_GPIO GPIO_NUM_8

static const char *TAG = "MAIN";

// Global application configuration
app_config_t app_config;

void blink_led(int times) {
    for (int i = 0; i < times; i++) {
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(250 / portTICK_PERIOD_MS);
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
}

void app_main() {
    #if CONFIG_PM_ENABLE
        esp_pm_config_t pm_config = {
            .max_freq_mhz = 160,
            .min_freq_mhz = 160,
            .light_sleep_enable = false
        };
        ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
    #endif

    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    blink_led(2);

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
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    wifi_manager_init(&app_config);
    ambitful_ble_init(&app_config);
    start_webserver(&app_config); // Pass config to webserver
    start_artnet_server(&app_config);
}
