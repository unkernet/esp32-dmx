#include "app_config_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "app_config.h"
#include <string.h>

static const char *TAG = "APP_CONFIG_NVS";

esp_err_t app_config_nvs_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t app_config_load(app_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle for config!", esp_err_to_name(err));
        app_config_get_default(config); // Load defaults if NVS cannot be opened
        return err;
    }

    size_t required_size = sizeof(app_config_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_APP_CONFIG, config, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Config not found in NVS, loading default values.");
        app_config_get_default(config);
        err = ESP_OK; // Treat as success, defaults loaded
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) reading config from NVS!", esp_err_to_name(err));
        app_config_get_default(config); // Load defaults on read error
    } else if (required_size != sizeof(app_config_t)) {
        ESP_LOGW(TAG, "Config size mismatch! Expected %d, got %d. Loading defaults.",
                 sizeof(app_config_t), required_size);
        app_config_get_default(config); // Load defaults on size mismatch
        err = ESP_ERR_NVS_INVALID_LENGTH; // Indicate size mismatch
    } else {
        ESP_LOGI(TAG, "Config loaded successfully from NVS.");
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t app_config_save(const app_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle for config!", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs_handle, NVS_KEY_APP_CONFIG, config, sizeof(app_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) writing config to NVS!", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) committing config to NVS!", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Config saved successfully to NVS.");
    }

    nvs_close(nvs_handle);
    return err;
}
