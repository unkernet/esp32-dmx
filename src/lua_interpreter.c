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
        free(filename);
        lua_task_handle = NULL;
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
    } else {
        ESP_LOGI(TAG, "Script finished successfully");
    }

    lua_close(L);
    free(filename);
    lua_task_handle = NULL;
    ESP_LOGI(TAG, "Lua task finished");
    vTaskDelete(NULL);
}

esp_err_t lua_interpreter_init(void) {
    return ESP_OK;
}

esp_err_t lua_interpreter_run(const char *filename) {
    if (lua_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    should_stop = false;
    char full_path[128];
    if (filename[0] != '/') {
        snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);
    } else {
        strncpy(full_path, filename, sizeof(full_path));
    }

    char *fn_copy = strdup(full_path);
    xTaskCreate(lua_task, "lua_task", 8192, fn_copy, 5, &lua_task_handle);
    if (lua_task_handle == NULL) {
        free(fn_copy);
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
    }
    return ESP_OK;
}

bool lua_interpreter_is_running(void) {
    return lua_task_handle != NULL;
}

char* lua_interpreter_list_scripts(void) {
    DIR *dir = opendir("/spiffs");
    if (dir == NULL) {
        return strdup("[]");
    }

    size_t buf_size = 1024;
    char *json = malloc(buf_size);
    strcpy(json, "[");
    bool first = true;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, ".lua")) {
            if (!first) strcat(json, ",");
            
            // Check if we need more space
            if (strlen(json) + strlen(ent->d_name) + 5 > buf_size) {
                buf_size *= 2;
                json = realloc(json, buf_size);
            }

            strcat(json, "\"");
            strcat(json, ent->d_name);
            strcat(json, "\"");
            first = false;
        }
    }
    strcat(json, "]");
    closedir(dir);
    return json;
}
