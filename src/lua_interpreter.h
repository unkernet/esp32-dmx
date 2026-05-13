#ifndef LUA_INTERPRETER_H
#define LUA_INTERPRETER_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief Initialize the Lua interpreter environment.
 */
esp_err_t lua_interpreter_init(void);

/**
 * @brief Run a Lua script from SPIFFS.
 * 
 * @param filename The path to the script (e.g., "/spiffs/script.lua").
 * @return ESP_OK if the script started, ESP_ERR_INVALID_STATE if a script is already running.
 */
esp_err_t lua_interpreter_run(const char *filename);

/**
 * @brief Run a Lua script from an HTTP stream.
 * 
 * @param req The HTTP request handle to read from.
 * @return ESP_OK if the script started and finished loading.
 */
esp_err_t lua_interpreter_run_stream(httpd_req_t *req);

/**
 * @brief Stop the currently running Lua script.
 * 
 * @return ESP_OK if the script was stopped or wasn't running.
 */
esp_err_t lua_interpreter_kill(void);

/**
 * @brief Check if a Lua script is currently running.
 * 
 * @return true if running, false otherwise.
 */
bool lua_interpreter_is_running(void);

#include "esp_http_server.h"

/**
 * @brief Stream the list of Lua scripts and status as JSON to the HTTP response.
 * 
 * @param req The HTTP request handle to stream to.
 * @return ESP_OK on success.
 */
esp_err_t lua_interpreter_stream_scripts(httpd_req_t *req);

void send_lua_data(uint16_t universe, const uint8_t *data, uint16_t length);

#endif // LUA_INTERPRETER_H
