#include "lua_interpreter.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "router.h"
#include "esp_random.h"
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "LUA";
static TaskHandle_t lua_task_handle = NULL;
static volatile bool should_stop = false;
static char current_script[64] = "";
static char last_error[64] = "";
static SemaphoreHandle_t dmx_data_sem = NULL;
static SemaphoreHandle_t data_ready_sem = NULL;
static uint8_t dmx_buffer[512];
static uint16_t dmx_buffer_len = 0;
static uint16_t listen_universe = -1;
static uint16_t buffered_universe = -1;

static int l_dmx_send(lua_State *L) {
    int universe = luaL_checkinteger(L, 1);
    
    int type = lua_type(L, 2);
    if (type == LUA_TTABLE) {
        uint8_t data[512];
        size_t len = lua_rawlen(L, 2);
        if (len > 512) len = 512;
        for (size_t i = 0; i < len; i++) {
            lua_rawgeti(L, 2, i + 1);
            data[i] = (uint8_t)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        route_dmx_data(DATA_SOURCE_LUA, universe, data, len);
    } else if (type == LUA_TSTRING) {
        size_t str_len;
        const char *str = lua_tolstring(L, 2, &str_len);
        size_t len = (str_len > 512) ? 512 : str_len;
        route_dmx_data(DATA_SOURCE_LUA, universe, (const uint8_t *)str, len);
    } else {
        return luaL_error(L, "DMX data must be a table or string");
    }
    
    return 0;
}

static const char *KILLED_SENTINEL = "KILLED";

static void lua_kill_hook(lua_State *L, lua_Debug *ar) {
    if (should_stop) {
        lua_pushlightuserdata(L, (void *)KILLED_SENTINEL);
        lua_error(L);
    }
}

void send_lua_data(uint16_t universe, const uint8_t *data, uint16_t length) {
    if (universe == listen_universe) {
        if (xSemaphoreTake(dmx_data_sem, 0) == pdTRUE) {
            dmx_buffer_len = (length > sizeof(dmx_buffer)) ? sizeof(dmx_buffer) : length;
            memcpy(dmx_buffer, data, dmx_buffer_len);
            buffered_universe = universe;
            xSemaphoreGive(dmx_data_sem);
            xSemaphoreGive(data_ready_sem);
        }
    }
}

static int l_dmx_read(lua_State *L) {
    int universe = luaL_checkinteger(L, 1);
    int timeout = luaL_checkinteger(L, 2);

    listen_universe = universe;

    if (xSemaphoreTake(data_ready_sem, pdMS_TO_TICKS(timeout)) == pdTRUE) {
        lua_kill_hook(L, NULL);
        if (xSemaphoreTake(dmx_data_sem, portMAX_DELAY) == pdTRUE) {
            if (buffered_universe == (uint16_t)universe) {
                lua_pushlstring(L, (const char *)dmx_buffer, dmx_buffer_len);
                xSemaphoreGive(dmx_data_sem);
                return 1;
            }
            xSemaphoreGive(dmx_data_sem);
        }
    }

    lua_pushnil(L);
    return 1;
}

static int l_sleep(lua_State *L) {
    int ms = luaL_checkinteger(L, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    lua_kill_hook(L, NULL);
    return 0;
}

static int l_random(lua_State *L) {
    lua_pushinteger(L, esp_random());
    return 1;
}

static void lua_task(void *pvParameters) {
    char full_path[80];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", current_script);

    lua_State *L = luaL_newstate();
    if (L == NULL) {
        ESP_LOGE(TAG, "Failed to create Lua state");
        strncpy(last_error, "Failed to create Lua state", sizeof(last_error) - 1);
        lua_task_handle = NULL;
        current_script[0] = '\0';
        vTaskDelete(NULL);
        return;
    }

    luaL_openlibs(L);
    
    // Register custom functions
    lua_register(L, "dmx_send", l_dmx_send);
    lua_register(L, "dmx_read", l_dmx_read);
    lua_register(L, "random", l_random);
    lua_register(L, "sleep", l_sleep);

    // Set hook to allow killing the script
    lua_sethook(L, lua_kill_hook, LUA_MASKCOUNT, 100);

    ESP_LOGI(TAG, "Running script: %s", current_script);
    
    int status = luaL_dofile(L, full_path);
    if (status != LUA_OK) {
        if (lua_islightuserdata(L, -1) && lua_touserdata(L, -1) == (void *)KILLED_SENTINEL) {
            ESP_LOGI(TAG, "Script was killed");
            last_error[0] = '\0';
        } else {
            const char *error = lua_tostring(L, -1);
            ESP_LOGE(TAG, "Lua error: %s", error);
            strncpy(last_error, error, sizeof(last_error) - 1);
            last_error[sizeof(last_error) - 1] = '\0';
        }
    } else {
        ESP_LOGI(TAG, "Script finished");
        last_error[0] = '\0';
    }

    lua_close(L);
    lua_task_handle = NULL;
    current_script[0] = '\0';
    listen_universe = -1;
    vTaskDelete(NULL);
}

esp_err_t lua_interpreter_init(void) {
    data_ready_sem = xSemaphoreCreateBinary();
    dmx_data_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(dmx_data_sem);

    struct stat st;
    if (stat("/spiffs/init.lua", &st) == 0) {
        ESP_LOGI(TAG, "Found init.lua, starting...");
        return lua_interpreter_run("init.lua");
    }
    return ESP_OK;
}

esp_err_t lua_interpreter_run(const char *filename) {
    if (lua_task_handle != NULL) {
        lua_interpreter_kill();
    }

    should_stop = false;
    last_error[0] = '\0';
    strncpy(current_script, filename, sizeof(current_script) - 1);
    listen_universe = -1;

    xTaskCreate(lua_task, "lua_task", 8192, NULL, 5, &lua_task_handle);
    if (lua_task_handle == NULL) {
        current_script[0] = '\0';
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t lua_interpreter_kill(void) {
    if (lua_task_handle == NULL) {
        return ESP_OK;
    }
    should_stop = true;
    
    // Abort any pending delay (sleep) immediately
    xTaskAbortDelay(lua_task_handle);
    xSemaphoreGive(data_ready_sem);
    
    // Wait a bit for it to stop gracefully
    int timeout = 100; // 1 second
    while (lua_task_handle != NULL && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    TaskHandle_t target_handle = lua_task_handle;
    if (target_handle != NULL) {
        ESP_LOGW(TAG, "Script was forcibly killed");
        strncpy(last_error, "Script was forcibly killed", sizeof(last_error) - 1);
        vTaskDelete(target_handle);
        lua_task_handle = NULL;
        current_script[0] = '\0';
        listen_universe = -1;
    }
    return ESP_OK;
}

bool lua_interpreter_is_running(void) {
    return lua_task_handle != NULL;
}

esp_err_t lua_interpreter_stream_scripts(httpd_req_t *req) {
    DIR *dir = opendir("/spiffs");
    if (dir == NULL) {
        httpd_resp_sendstr(req, "{\"scripts\":[],\"running\":null,\"error\":null}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "{\"scripts\":[", HTTPD_RESP_USE_STRLEN);

    struct dirent *ent;
    bool first = true;
    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, ".lua")) {
            char buf[128];
            int len = snprintf(buf, sizeof(buf), "%s\"%s\"", first ? "" : ",", ent->d_name);
            httpd_resp_send_chunk(req, buf, len);
            first = false;
        }
    }
    closedir(dir);

    char status_buf[192];
    int status_len;
    
    const char *running = lua_interpreter_is_running() ? current_script : NULL;
    const char *error = (last_error[0] != '\0') ? last_error : NULL;

    status_len = snprintf(status_buf, sizeof(status_buf), 
        "],\"running\":%s%s%s,\"error\":%s%s%s}",
        running ? "\"" : "", running ? running : "null", running ? "\"" : "",
        error ? "\"" : "", error ? error : "null", error ? "\"" : "");

    httpd_resp_send_chunk(req, status_buf, status_len);

    // Finish chunked response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}
