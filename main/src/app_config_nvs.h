#ifndef APP_CONFIG_NVS_H
#define APP_CONFIG_NVS_H

#include "esp_err.h"
#include "app_config.h"

#define NVS_NAMESPACE "config"
#define NVS_KEY_APP_CONFIG "app_config"

/**
 * @brief Initializes NVS. Must be called before any NVS operations.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t app_config_nvs_init(void);

/**
 * @brief Loads the application configuration from NVS.
 *        If no configuration is found, it loads default values.
 * @param config Pointer to the app_config_t structure to fill.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t app_config_load(app_config_t *config);

/**
 * @brief Saves the application configuration to NVS.
 * @param config Pointer to the app_config_t structure to save.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t app_config_save(const app_config_t *config);

#endif // APP_CONFIG_NVS_H
