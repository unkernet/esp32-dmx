#include "lua_interpreter.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "app_config.h"
#include "router.h"
#include "util.h"
#include "modules.h"
#include "esp_random.h"
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#define DATA_NOTIFY (0) // Increase CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES if set greater then 0
#define EMPTY (-1)
#define C_SCRIPT_LEN 64
#define ERROR_LEN 128
static const char *TAG = "LUA";

typedef struct {
    TaskHandle_t task_handle;
    lua_State *L;
    volatile bool should_stop;
    char current_script[C_SCRIPT_LEN];
    char last_error[ERROR_LEN];
    
    // DMX Data exchange
    SemaphoreHandle_t dmx_data_mutex;
    uint8_t dmx_buffer[DMX_LEN];
    uint16_t dmx_buffer_len;
    uint16_t listen_universe;
    uint16_t buffered_universe;
} lua_interpreter_state_t;

typedef struct {
    const char *filename; // NULL for stream
    httpd_req_t *req;      // NULL for file
    char *buf;
    size_t buf_len;
    SemaphoreHandle_t load_sem;
    esp_err_t result;
} lua_load_ctx_t;

static lua_interpreter_state_t *S = NULL;

static int l_dmx_send(lua_State *L) {
    int universe = luaL_checkinteger(L, 1);
    uint8_t debug = lua_toboolean(L, 3);
    
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
        route_dmx_data(debug ? DATA_SOURCE_LUA_DEBUG : DATA_SOURCE_LUA, universe, data, len);
    } else if (type == LUA_TSTRING) {
        size_t str_len;
        const char *str = lua_tolstring(L, 2, &str_len);
        route_dmx_data(debug ? DATA_SOURCE_LUA_DEBUG : DATA_SOURCE_LUA, universe, (const uint8_t *)str, str_len);
    } else {
        return luaL_error(L, "DMX data must be a table or string");
    }

    // Script will run much more stable with regular garbage collection
    lua_gc(L, LUA_GCSTEP, 0);

    return 0;
}

static int l_print(lua_State *L) {
    size_t str_len;
    const char *str = luaL_tolstring(L, 1, &str_len);
    ESP_LOGI(TAG, "%s", str);
    lua_pop(L, 1);
    return 0;
}

static void l_warn(void *ud, const char *msg, int tocont) {
    ESP_LOGW(TAG, "%s", msg);
}

static const char *KILLED_SENTINEL = "KILLED";

static void lua_kill_hook(lua_State *L, lua_Debug *ar) {
    if (S && S->should_stop) {
        lua_pushlightuserdata(L, (void *)KILLED_SENTINEL);
        lua_error(L);
    }
}

void send_lua_data(uint16_t universe, const uint8_t *data, uint16_t length) {
    if (!S || universe != S->listen_universe) {
        return;
    }

    TaskHandle_t task = S->task_handle;
    if (task == NULL) {
        return;
    }

    if (xSemaphoreTake(S->dmx_data_mutex, 0) == pdTRUE) {
        S->dmx_buffer_len = (length > DMX_LEN) ? DMX_LEN : length;
        memcpy(S->dmx_buffer, data, S->dmx_buffer_len);
        S->buffered_universe = universe;
        xSemaphoreGive(S->dmx_data_mutex);
        xTaskNotifyGiveIndexed(task, DATA_NOTIFY);
    }
}

static int l_dmx_read(lua_State *L) {
    lua_kill_hook(L, NULL);

    int universe = luaL_checkinteger(L, 1);
    int timeout = luaL_checkinteger(L, 2);

    if (!S) return luaL_error(L, "Interpreter state missing");

    // Keep listen even after TaskNotify timeout to be able to receive the data on next dmx_read() call
    S->listen_universe = universe;

    if (ulTaskNotifyTakeIndexed(DATA_NOTIFY, pdTRUE, pdMS_TO_TICKS(timeout)) > 0) {
        lua_kill_hook(L, NULL);
        if (xSemaphoreTake(S->dmx_data_mutex, portMAX_DELAY) == pdTRUE) {
            if (S->buffered_universe == (uint16_t)universe) {
                lua_pushlstring(L, (const char *)S->dmx_buffer, S->dmx_buffer_len);
                S->buffered_universe = EMPTY; // Mark data as consumed
                xSemaphoreGive(S->dmx_data_mutex);
                return 1;
            }
            xSemaphoreGive(S->dmx_data_mutex);
        }
    }

    lua_pushnil(L);
    return 1;
}

static int l_sleep(lua_State *L) {
    lua_kill_hook(L, NULL);

    int ms = luaL_checkinteger(L, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    lua_kill_hook(L, NULL);
    return 0;
}

static int l_random(lua_State *L) {
    int n = lua_gettop(L);
    if (n == 0) {
        lua_pushinteger(L, esp_random());
    } else if (n == 1) {
        int max = (int)luaL_checkinteger(L, 1);
        if (max < 1) return luaL_error(L, "max must be >= 1");
        lua_pushinteger(L, (esp_random() % max) + 1);
    } else {
        int min = (int)luaL_checkinteger(L, 1);
        int max = (int)luaL_checkinteger(L, 2);
        if (max < min) return luaL_error(L, "max must be >= min");
        lua_pushinteger(L, (esp_random() % (max - min + 1)) + min);
    }
    return 1;
}

static esp_err_t ensure_lua_state() {
    if (S == NULL) {
        S = calloc(1, sizeof(lua_interpreter_state_t));
        RETURN_ON_NULL(S, ESP_ERR_NO_MEM);
        
        if(unlikely(!(S->dmx_data_mutex = xSemaphoreCreateMutex()))) {
            free(S);
            S = NULL;
            return ESP_ERR_NO_MEM;
        }
        
        S->listen_universe = EMPTY;
        S->buffered_universe = EMPTY;
    }
    return ESP_OK;
}

static const char* lua_stream_reader(lua_State *L, void *data, size_t *size) {
    lua_load_ctx_t *ctx = (lua_load_ctx_t *)data;
    int received = httpd_req_recv(ctx->req, ctx->buf, ctx->buf_len);
    if (received <= 0) {
        *size = 0;
        return NULL;
    }
    *size = (size_t)received;
    return ctx->buf;
}

static void lua_task(void *pvParameters) {
    lua_load_ctx_t *ctx = (lua_load_ctx_t *)pvParameters;

    lua_State *L = luaL_newstate();
    S->L = L;
    if (L == NULL) {
        ESP_LOGE(TAG, "Failed to create Lua state");
        strncpy(S->last_error, "Failed to create Lua state", ERROR_LEN - 1);
        ctx->result = ESP_ERR_NO_MEM;
        xSemaphoreGive(ctx->load_sem);
        S->task_handle = NULL;
        S->current_script[0] = '\0';
        vTaskDelete(NULL);
        return;
    }

    luaL_openlibs(L);

    // Remove prohibited libraries and functions
    lua_pushnil(L);
    lua_setglobal(L, "io");
    lua_getglobal(L, "os");
    if (lua_istable(L, -1)) {
        const char *restricted[] = {"execute", "getenv", "remove", "rename", "tmpname", "exit", "setlocale", NULL};
        for (int i = 0; restricted[i]; i++) {
            lua_pushnil(L);
            lua_setfield(L, -2, restricted[i]);
        }
    }
    lua_pop(L, 1); // pop os
    lua_getglobal(L, "package");
    if (lua_istable(L, -1)) {
        lua_pushliteral(L, "/user/?.luac;/user/?.lua;/user/?/init.luac;/user/?/init.lua");
        lua_setfield(L, -2, "path");
    }
    lua_pop(L, 1); // pop package

    // Register dmx library
    const luaL_Reg dmx_lib[] = {
        {"send", l_dmx_send},
        {"read", l_dmx_read},
        {NULL, NULL}
    };
    lua_newtable(L);
    luaL_setfuncs(L, dmx_lib, 0);
    lua_setglobal(L, "dmx");
    
    // Register global custom functions
    lua_register(L, "random", l_random);
    lua_register(L, "sleep", l_sleep);
    lua_register(L, "print", l_print);
    lua_setwarnf(L, l_warn, NULL);

    int status;
    if (ctx->req) {
        ESP_LOGI(TAG, "Loading script from stream");
        status = lua_load(L, lua_stream_reader, ctx, "stream", NULL);
    } else {
        char full_path[80];
        snprintf(full_path, sizeof(full_path), "/user/%s", ctx->filename);
        ESP_LOGI(TAG, "Loading script from file: %s", ctx->filename);
        status = luaL_loadfile(L, full_path);
    }

    if (status != LUA_OK) {
        const char *error = lua_tostring(L, -1);
        ESP_LOGE(TAG, "Lua load error: %s", error);
        strncpy(S->last_error, error, ERROR_LEN - 1);
        S->last_error[ERROR_LEN - 1] = '\0';
        ctx->result = ESP_OK;
        xSemaphoreGive(ctx->load_sem);
    } else {
        strncpy(S->current_script, ctx->filename ? ctx->filename : "---", C_SCRIPT_LEN - 1);
        S->should_stop = false;
        S->last_error[0] = '\0';
        S->listen_universe = EMPTY;

        ctx->result = ESP_OK;
        xSemaphoreGive(ctx->load_sem);

        // Execute the script
        status = lua_pcall(L, 0, LUA_MULTRET, 0);
        if (status != LUA_OK) {
            if (lua_islightuserdata(L, -1) && lua_touserdata(L, -1) == (void *)KILLED_SENTINEL) {
                ESP_LOGI(TAG, "Script was killed");
                S->last_error[0] = '\0';
            } else {
                const char *error = lua_tostring(L, -1);
                ESP_LOGE(TAG, "Lua runtime error: %s", error);
                strncpy(S->last_error, error, ERROR_LEN - 1);
                S->last_error[ERROR_LEN - 1] = '\0';
            }
        } else {
            ESP_LOGI(TAG, "Script finished");
            S->last_error[0] = '\0';
        }
    }

    lua_close(L);
    S->L = NULL;
    S->task_handle = NULL;
    S->current_script[0] = '\0';
    S->listen_universe = EMPTY;
    vTaskDelete(NULL);
}

esp_err_t lua_interpreter_init(void) {
    RETURN_ON_ERROR(ensure_lua_state());

    struct stat st;
    if (stat("/user/init.lua", &st) == 0) {
        return lua_interpreter_run("init.lua");
    }
    return ESP_OK;
}

static esp_err_t lua_interpreter_run_internal(lua_load_ctx_t *ctx) {
    RETURN_ON_ERROR(ensure_lua_state());
    if (S->task_handle != NULL) {
        lua_interpreter_kill();
    }

    ctx->load_sem = xSemaphoreCreateBinary();
    if (!ctx->load_sem) return ESP_ERR_NO_MEM;

    xTaskCreate(lua_task, "lua_task", 8192, ctx, 5, &S->task_handle);
    if (S->task_handle == NULL) {
        vSemaphoreDelete(ctx->load_sem);
        return ESP_ERR_NO_MEM;
    }

    // Wait for the script to finish loading (compilation)
    xSemaphoreTake(ctx->load_sem, portMAX_DELAY);
    vSemaphoreDelete(ctx->load_sem);

    return ctx->result;
}

esp_err_t lua_interpreter_run(const char *filename) {
    lua_load_ctx_t ctx = {
        .filename = filename,
        .req = NULL,
        .buf = NULL,
        .buf_len = 0,
    };
    return lua_interpreter_run_internal(&ctx);
}

esp_err_t lua_interpreter_run_stream(httpd_req_t *req) {
    lua_load_ctx_t ctx = {
        .filename = NULL,
        .req = req,
        .buf_len = 512,
    };
    ctx.buf = malloc(ctx.buf_len);
    if (!ctx.buf) return ESP_ERR_NO_MEM;

    esp_err_t err = lua_interpreter_run_internal(&ctx);
    free(ctx.buf);
    return err;
}

esp_err_t lua_interpreter_kill(void) {
    if (!S) {
        return ESP_OK;
    }

    TaskHandle_t task_handle = S->task_handle;
    if (task_handle == NULL) {
        return ESP_OK;
    }

    S->should_stop = true;
    
    lua_State *L = S->L;
    if (L) {
        // Set hook to allow killing the script
        lua_sethook(L, lua_kill_hook, LUA_MASKCOUNT, 1);
    }

    // Abort any pending delay (sleep) immediately
    xTaskNotifyGiveIndexed(task_handle, DATA_NOTIFY);
    xTaskAbortDelay(task_handle);
    
    // Wait a bit for it to stop gracefully
    int timeout = 100; // 1 second
    while (S->task_handle != NULL && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    task_handle = S->task_handle;

    if (task_handle != NULL) {
        ESP_LOGW(TAG, "Script was forcibly killed");
        strncpy(S->last_error, "Script was forcibly killed", ERROR_LEN - 1);
        vTaskDelete(task_handle);
        S->L = NULL;
        S->task_handle = NULL;
        S->current_script[0] = '\0';
        S->listen_universe = EMPTY;
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

bool lua_interpreter_is_running(void) {
    return S && S->task_handle != NULL;
}

esp_err_t lua_interpreter_list_scripts(httpd_req_t *req) {
    DIR *dir = opendir("/user");
    if (dir == NULL) {
        httpd_resp_sendstr(req, "{\"scripts\":[],\"running\":null,\"error\":null}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "{\"scripts\":[", HTTPD_RESP_USE_STRLEN);

    struct dirent *ent;
    bool first = true;
    while ((ent = readdir(dir)) != NULL) {
        if (ends_with(ent->d_name, ".lua") || ends_with(ent->d_name, ".luac")) {
            char buf[48];
            int len = snprintf(buf, sizeof(buf), "%s\"%s\"", first ? "" : ",", ent->d_name);
            httpd_resp_send_chunk(req, buf, len);
            first = false;
        }
    }
    closedir(dir);

    char status_buf[ERROR_LEN + C_SCRIPT_LEN + 32];
    int status_len;
    
    // Escape last_error
    if (S) {
        for (char *p = S->last_error; *p && (p < S->last_error + ERROR_LEN); p++) {
            if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r' || *p == '\t')
                *p = ' ';
        }
    }

    const char *running = lua_interpreter_is_running() ? S->current_script : NULL;
    const char *error = (S && S->last_error[0] != '\0') ? S->last_error : NULL;

    status_len = snprintf(status_buf, sizeof(status_buf), 
        "],\"running\":%s%s%s,\"error\":%s%s%s}",
        running ? "\"" : "", running ? running : "null", running ? "\"" : "",
        error ? "\"" : "", error ? error : "null", error ? "\"" : "");

    httpd_resp_send_chunk(req, status_buf, status_len);

    // Finish chunked response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}
