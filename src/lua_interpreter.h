#ifndef LUA_INTERPRETER_H
#define LUA_INTERPRETER_H

#include <stdbool.h>
#include "esp_err.h"

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

/**
 * @brief List all Lua scripts in the /spiffs directory.
 * 
 * @return A JSON string representing the list of scripts. Caller must free().
 */
char* lua_interpreter_list_scripts(void);

#endif // LUA_INTERPRETER_H
