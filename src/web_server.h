#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"
#include "app_config.h"

/**
 * @brief Start the internal web server.
 * @param config Pointer to the application configuration.
 * @return httpd_handle_t The web server handle, or NULL if failed.
 */
httpd_handle_t start_webserver(app_config_t *config);

/**
 * @brief Send DMX data via WebSocket to the active client.
 * @param universe The target DMX universe.
 * @param data Pointer to the DMX payload.
 * @param length Length of the payload.
 */
void send_ws_dmx_data(uint16_t universe, const uint8_t *data, uint16_t length);

#endif // WEB_SERVER_H
