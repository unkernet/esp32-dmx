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
#include "ble_beacon.h"

#define BLINK_GPIO GPIO_NUM_8

static const char *TAG = "MAIN";

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
            .min_freq_mhz = 80,
            .light_sleep_enable = false
        };
        ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
    #endif

    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    blink_led(2);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };
    
    ret = esp_vfs_spiffs_register(&conf);

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

    ble_beacon_init();

    char ssid[32];
    char password[64];

    if (read_wifi_config(ssid, sizeof(ssid), password, sizeof(password)) == ESP_OK) {
        ESP_LOGI(TAG, "Found stored WiFi config: SSID=%s", ssid);
        wifi_init_sta(ssid, password);
    } else {
        ESP_LOGI(TAG, "No WiFi config found, starting AP mode.");
        wifi_init_ap();
    }
    start_webserver();
    xTaskCreate(artnet_server_task, "artnet_server", 4096, NULL, 5, NULL);
}
