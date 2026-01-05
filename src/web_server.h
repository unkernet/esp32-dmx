#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"
#include "app_config.h" // Include the new config header

httpd_handle_t start_webserver(app_config_t *config);
void send_ws_dmx_data(uint16_t universe, const uint8_t * data, uint16_t length);

#endif // WEB_SERVER_H
