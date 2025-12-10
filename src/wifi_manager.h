#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "app_config.h" // Include the new config header

// Function to initialize WiFi manager and set up STA or AP based on config
void wifi_manager_init(app_config_t *config);

#endif // WIFI_MANAGER_H
