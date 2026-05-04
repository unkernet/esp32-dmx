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

static const char *TAG = "LUA_INT";
static TaskHandle_t lua_task_handle = NULL;
static volatile bool should_stop = false;
static char current_script[64] = "";
static char last_error[64] = "";

static int l_send_dmx(lua_State *L) {
    int universe = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    
    size_t len = lua_rawlen(L, 2);
    if (len > 512) len = 512;
    
    uint8_t data[512];
    for (int i = 1; i <= len; i++) {
        lua_rawgeti(L, 2, i);
        data[i-1] = (uint8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    
    route_dmx_data(DATA_SOURCE_LUA, universe, data, len);
    return 0;
}

static int l_sleep(lua_State *L) {
    int ms = luaL_checkinteger(L, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    return 0;
}

static int l_random(lua_State *L) {
    lua_pushinteger(L, esp_random());
    return 1;
}

static void lua_hook(lua_State *L, lua_Debug *ar) {
    if (should_stop) {
        luaL_error(L, "Script killed");
    }
}

static void lua_task(void *pvParameters) {
    char *filename = (char *)pvParameters;
    lua_State *L = luaL_newstate();
    if (L == NULL) {
        ESP_LOGE(TAG, "Failed to create Lua state");
        strncpy(last_error, "Failed to create Lua state", sizeof(last_error) - 1);
        free(filename);
        lua_task_handle = NULL;
        current_script[0] = '\0';
        vTaskDelete(NULL);
        return;
    }

    luaL_openlibs(L);
    
    // Register custom functions
    lua_register(L, "send_dmx", l_send_dmx);
    lua_register(L, "random", l_random);
    lua_register(L, "sleep", l_sleep);

    // Set hook to allow killing the script
    lua_sethook(L, lua_hook, LUA_MASKCOUNT, 100);

    ESP_LOGI(TAG, "Running script: %s", filename);
    if (luaL_dofile(L, filename) != LUA_OK) {
        const char *error = lua_tostring(L, -1);
        ESP_LOGE(TAG, "Lua error: %s", error);
        strncpy(last_error, error, sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
    } else {
        ESP_LOGI(TAG, "Script finished successfully");
        last_error[0] = '\0';
    }

    lua_close(L);
    free(filename);
    lua_task_handle = NULL;
    current_script[0] = '\0';
    ESP_LOGI(TAG, "Lua task finished");
    vTaskDelete(NULL);
}

esp_err_t lua_interpreter_init(void) {
    struct stat st;
    if (stat("/spiffs/init.lua", &st) == 0) {
        ESP_LOGI(TAG, "Found init.lua, starting...");
        return lua_interpreter_run("init.lua");
    }
    return ESP_OK;
}

esp_err_t lua_interpreter_run(const char *filename) {
    if (lua_task_handle != NULL) {
        ESP_LOGI(TAG, "Killing currently running script to start %s", filename);
        lua_interpreter_kill();
    }

    should_stop = false;
    last_error[0] = '\0';
    char full_path[128];
    if (filename[0] != '/') {
        snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);
    } else {
        strncpy(full_path, filename, sizeof(full_path));
    }

    // Store simple name for status
    const char *last_slash = strrchr(filename, '/');
    strncpy(current_script, last_slash ? last_slash + 1 : filename, sizeof(current_script) - 1);

    char *fn_copy = strdup(full_path);
    xTaskCreate(lua_task, "lua_task", 8192, fn_copy, 5, &lua_task_handle);
    if (lua_task_handle == NULL) {
        free(fn_copy);
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
    
    // Wait a bit for it to stop gracefully
    int timeout = 100; // 1 second
    while (lua_task_handle != NULL && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    if (lua_task_handle != NULL) {
        ESP_LOGW(TAG, "Forcibly deleting Lua task");
        vTaskDelete(lua_task_handle);
        lua_task_handle = NULL;
        current_script[0] = '\0';
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
