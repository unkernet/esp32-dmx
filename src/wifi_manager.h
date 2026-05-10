#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "app_config.h" // Include the new config header

#include "esp_http_server.h"

// Function to initialize WiFi manager and set up STA or AP based on config
esp_err_t wifi_manager_init(app_config_t *config);

esp_err_t wifi_manager_scan_wifi(httpd_req_t *req);

#endif // WIFI_MANAGER_H
