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
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/queue.h"

static_assert(sizeof(lua_Integer) == 4 && sizeof(lua_Number) == 4,
    "Lua API width disagrees with liblua build (LUA_32BITS missing?)");

#define DATA_NOTIFY (0) // Increase CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES if set greater then 0
#define EMPTY (UINT16_MAX)
#define C_SCRIPT_LEN 64
#define ERROR_LEN 128
#define MAX_UNIVERSE_CB 4
#define MAX_TIMERS      8
#define EVT_QUEUE_LEN   8
#define MAX_UNIVERSE    32768
static const char *TAG = "LUA";

typedef enum: uint8_t {
    LUA_STATE_IDLE,
    LUA_STATE_RUNNING,
    LUA_STATE_SLEEPING
} lua_run_state_t;

typedef enum {
    EVT_DMX,
    EVT_SHUTDOWN
} lua_event_type_t;

typedef struct {
    lua_event_type_t type;
    uint16_t universe;
} lua_event_t;

typedef struct {
    uint16_t universe;   // EMPTY = free slot
    int lua_ref;         // LUA_NOREF = free slot
} lua_universe_cb_t;

typedef struct {
    uint32_t id;         // 0 = free slot
    TickType_t deadline;
    TickType_t period;   // 0 = one-shot
    int lua_ref;
} lua_timer_t;

typedef struct __attribute__((packed)) {
    volatile lua_run_state_t run_state;
    char current_script[C_SCRIPT_LEN];
    char last_error[ERROR_LEN];
} script_state_t;

typedef struct {
    TaskHandle_t task_handle;
    lua_State *L;
    volatile bool should_stop;
    script_state_t script_state;

    // DMX Data exchange
    SemaphoreHandle_t dmx_data_mutex;
    uint8_t dmx_buffer[DMX_LEN];
    uint16_t dmx_buffer_len;
    uint16_t last_read_universe;
    uint16_t buffered_universe;

    // Event loop
    QueueHandle_t event_queue;
    lua_universe_cb_t universe_cb[MAX_UNIVERSE_CB];
    lua_timer_t timers[MAX_TIMERS];
    uint32_t next_timer_id;
    uint16_t active_callback_total;
} lua_interpreter_state_t;

typedef struct {
    const char *filename; // NULL for stream
    httpd_req_t *req;     // NULL for file
    char *buf;
    size_t buf_len;
    SemaphoreHandle_t load_sem;
    esp_err_t result;
} lua_load_ctx_t;

static lua_interpreter_state_t *S = NULL;

static uint16_t check_universe(lua_State *L, int arg) {
    int u = (int)luaL_checkinteger(L, arg);
    if (u < 0 || u > MAX_UNIVERSE) {
        luaL_error(L, "universe out of range (0..%d)", MAX_UNIVERSE);
    }
    return (uint16_t)u;
}

static int l_dmx_send(lua_State *L) {
    uint16_t universe = check_universe(L, 1);
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

static bool is_subscribed(uint16_t u) {
    if (u == S->last_read_universe) return true;
    for (int i = 0; i < MAX_UNIVERSE_CB; i++) {
        if (S->universe_cb[i].lua_ref != LUA_NOREF
            && S->universe_cb[i].universe == u) return true;
    }
    return false;
}

void send_lua_data(uint16_t universe, const uint8_t *data, uint16_t length) {
    if (!S || !is_subscribed(universe)) {
        return;
    }

    if (xSemaphoreTake(S->dmx_data_mutex, 0) == pdTRUE) {
        TaskHandle_t task = S->task_handle;
        if (task != NULL) {
            S->dmx_buffer_len = (length > DMX_LEN) ? DMX_LEN : length;
            memcpy(S->dmx_buffer, data, S->dmx_buffer_len);
            S->buffered_universe = universe;
            // Wake a blocking dmx.read() only if it's waiting on this universe.
            if (universe == S->last_read_universe) {
                xTaskNotifyGiveIndexed(task, DATA_NOTIFY);
            }
            if (S->event_queue) {
                lua_event_t evt = { .type = EVT_DMX, .universe = universe };
                xQueueSend(S->event_queue, &evt, 0); // non-blocking; drop on full
            }
        }
        xSemaphoreGive(S->dmx_data_mutex);
    }
}

static int l_dmx_read(lua_State *L) {
    lua_kill_hook(L, NULL);

    uint16_t universe = check_universe(L, 1);
    int timeout = (int)luaL_checkinteger(L, 2);

    if (!S) return luaL_error(L, "Interpreter state missing");

    // Keep listen even after TaskNotify timeout to be able to receive the data on next dmx_read() call
    if (S->last_read_universe != universe) {
        S->last_read_universe = universe;
        // Drop notifies that producer gated to the previous universe but were never consumed
        ulTaskNotifyValueClearIndexed(NULL, DATA_NOTIFY, ULONG_MAX);
    }

    if (ulTaskNotifyTakeIndexed(DATA_NOTIFY, pdTRUE, pdMS_TO_TICKS(timeout)) > 0) {
        lua_kill_hook(L, NULL);
        if (xSemaphoreTake(S->dmx_data_mutex, portMAX_DELAY) == pdTRUE) {
            if (S->buffered_universe == universe) {
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

    int ms = (int)luaL_checkinteger(L, 1);
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

static int l_esp_dmx_on(lua_State *L) {
    uint16_t universe = check_universe(L, 1);
    int fn_type = lua_type(L, 2);

    if (fn_type != LUA_TFUNCTION && fn_type != LUA_TNIL) {
        return luaL_error(L, "esp.dmx.on: handler must be function or nil");
    }

    int slot = -1, empty = -1;
    for (int i = 0; i < MAX_UNIVERSE_CB; i++) {
        if (S->universe_cb[i].lua_ref != LUA_NOREF
            && S->universe_cb[i].universe == universe) {
            slot = i;
            break;
        }
        if (S->universe_cb[i].lua_ref == LUA_NOREF && empty == -1) {
            empty = i;
        }
    }

    if (slot >= 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, S->universe_cb[slot].lua_ref);
        S->universe_cb[slot].lua_ref = LUA_NOREF;
        S->universe_cb[slot].universe = EMPTY;
        S->active_callback_total--;
    }

    if (fn_type == LUA_TNIL) return 0;

    if (slot < 0) slot = empty;
    if (slot < 0) {
        return luaL_error(L, "esp.dmx.on: no free callback slot (max %d)", MAX_UNIVERSE_CB);
    }
    lua_pushvalue(L, 2);
    S->universe_cb[slot].lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    S->universe_cb[slot].universe = universe;
    S->active_callback_total++;
    return 0;
}

static int l_esp_timer_register(lua_State *L, bool is_interval) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    int ms = (int)luaL_checkinteger(L, 2);
    if (ms < 0) return luaL_error(L, "interval must be >= 0");

    int empty = -1;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (S->timers[i].id == 0) { empty = i; break; }
    }
    if (empty < 0) {
        return luaL_error(L, "no free timer slot (max %d)", MAX_TIMERS);
    }

    TickType_t period = pdMS_TO_TICKS(ms);
    if (period == 0 && ms > 0) period = 1; // round-up to 1 tick if non-zero ms

    lua_pushvalue(L, 1);
    S->timers[empty].lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    S->timers[empty].deadline = xTaskGetTickCount() + period;
    S->timers[empty].period = is_interval ? period : 0;
    if (++S->next_timer_id == 0) S->next_timer_id = 1; // skip 0 on wraparound (collides with "free slot")
    S->timers[empty].id = S->next_timer_id;
    S->active_callback_total++;

    lua_pushinteger(L, S->timers[empty].id);
    return 1;
}

static int l_esp_set_timeout(lua_State *L)  { return l_esp_timer_register(L, false); }
static int l_esp_set_interval(lua_State *L) { return l_esp_timer_register(L, true); }

static int l_esp_clear_timer(lua_State *L) {
    uint32_t id = (uint32_t)luaL_checkinteger(L, 1);
    if (id == 0) return 0;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (S->timers[i].id == id) {
            luaL_unref(L, LUA_REGISTRYINDEX, S->timers[i].lua_ref);
            S->timers[i].id = 0;
            S->timers[i].lua_ref = LUA_NOREF;
            S->active_callback_total--;
            return 0;
        }
    }
    return 0; // silent on miss
}

static void reset_event_state(void) {
    for (int i = 0; i < MAX_UNIVERSE_CB; i++) {
        S->universe_cb[i].lua_ref = LUA_NOREF;
        S->universe_cb[i].universe = EMPTY;
    }
    for (int i = 0; i < MAX_TIMERS; i++) {
        S->timers[i].id = 0;
        S->timers[i].lua_ref = LUA_NOREF;
    }
    S->next_timer_id = 0;
    S->active_callback_total = 0;
    if (S->event_queue) xQueueReset(S->event_queue);
}

// Returns false on Lua error (last_error filled, loop should terminate)
static bool dispatch_dmx_event(lua_State *L, uint16_t universe) {
    int ref = LUA_NOREF;
    for (int i = 0; i < MAX_UNIVERSE_CB; i++) {
        if (S->universe_cb[i].lua_ref != LUA_NOREF
            && S->universe_cb[i].universe == universe) {
            ref = S->universe_cb[i].lua_ref;
            break;
        }
    }
    if (ref == LUA_NOREF) return true;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref); // [fn]

    bool stale = true;
    xSemaphoreTake(S->dmx_data_mutex, portMAX_DELAY);
    if (S->buffered_universe == universe) {
        lua_pushlstring(L, (const char *)S->dmx_buffer, S->dmx_buffer_len);
        S->buffered_universe = EMPTY;
        stale = false;
    }
    xSemaphoreGive(S->dmx_data_mutex);

    if (stale) {
        lua_pop(L, 1); // discard fn
        return true;
    }

    lua_pushinteger(L, universe);            // [fn, data, universe]
    int status = lua_pcall(L, 2, 0, 0);
    if (status != LUA_OK) {
        const char *err = luaL_tolstring(L, -1, NULL);
        strncpy(S->script_state.last_error, err, ERROR_LEN - 1);
        S->script_state.last_error[ERROR_LEN - 1] = '\0';
        lua_pop(L, 1);
        return false;
    }

    lua_gc(L, LUA_GCSTEP, 0);

    return true;
}

static bool fire_due_timers(lua_State *L) {
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (S->timers[i].id == 0) continue;
        if ((int32_t)(now - S->timers[i].deadline) < 0) continue;

        uint32_t snap_id = S->timers[i].id;
        int snap_ref = S->timers[i].lua_ref;
        TickType_t snap_period = S->timers[i].period;

        lua_rawgeti(L, LUA_REGISTRYINDEX, snap_ref);
        int status = lua_pcall(L, 0, 0, 0);
        if (status != LUA_OK) {
            const char *err = luaL_tolstring(L, -1, NULL);
            strncpy(S->script_state.last_error, err, ERROR_LEN - 1);
            S->script_state.last_error[ERROR_LEN - 1] = '\0';
            lua_pop(L, 1);
            return false;
        }

        // Callback may have cleared the timer (id == 0) or replaced it (id != snap_id)
        if (S->timers[i].id == snap_id) {
            if (snap_period == 0) {
                luaL_unref(L, LUA_REGISTRYINDEX, S->timers[i].lua_ref);
                S->timers[i].id = 0;
                S->timers[i].lua_ref = LUA_NOREF;
                S->active_callback_total--;
            } else {
                TickType_t cur = xTaskGetTickCount();
                do { S->timers[i].deadline += snap_period; }
                while ((int32_t)(cur - S->timers[i].deadline) >= 0);
            }
        }
    }
    return true;
}

static void run_event_loop(lua_State *L) {
    S->script_state.run_state = LUA_STATE_SLEEPING;
    ESP_LOGI(TAG, "Entering event loop");

    while (!S->should_stop && S->active_callback_total > 0) {
        TickType_t now = xTaskGetTickCount();
        TickType_t wait = portMAX_DELAY;
        for (int i = 0; i < MAX_TIMERS; i++) {
            if (S->timers[i].id == 0) continue;
            if ((int32_t)(now - S->timers[i].deadline) >= 0) { wait = 0; break; }
            TickType_t d = S->timers[i].deadline - now;
            if (d < wait) wait = d;
        }

        lua_event_t evt;
        if (xQueueReceive(S->event_queue, &evt, wait) == pdTRUE) {
            if (evt.type == EVT_SHUTDOWN) break;
            if (evt.type == EVT_DMX) {
                if (!dispatch_dmx_event(L, evt.universe)) break;
            }
        }

        if (S->should_stop) break;
        if (!fire_due_timers(L)) break;
    }

    for (int i = 0; i < MAX_UNIVERSE_CB; i++) {
        if (S->universe_cb[i].lua_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, S->universe_cb[i].lua_ref);
            S->universe_cb[i].lua_ref = LUA_NOREF;
            S->universe_cb[i].universe = EMPTY;
        }
    }
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (S->timers[i].id != 0) {
            luaL_unref(L, LUA_REGISTRYINDEX, S->timers[i].lua_ref);
            S->timers[i].id = 0;
            S->timers[i].lua_ref = LUA_NOREF;
        }
    }
    S->active_callback_total = 0;
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
        if(unlikely(!(S->event_queue = xQueueCreate(EVT_QUEUE_LEN, sizeof(lua_event_t))))) {
            vSemaphoreDelete(S->dmx_data_mutex);
            free(S);
            S = NULL;
            return ESP_ERR_NO_MEM;
        }

        S->last_read_universe = EMPTY;
        S->buffered_universe = EMPTY;
        S->script_state.run_state = LUA_STATE_IDLE;
        for (int i = 0; i < MAX_UNIVERSE_CB; i++) {
            S->universe_cb[i].lua_ref = LUA_NOREF;
            S->universe_cb[i].universe = EMPTY;
        }
        for (int i = 0; i < MAX_TIMERS; i++) {
            S->timers[i].lua_ref = LUA_NOREF;
        }
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
        strncpy(S->script_state.last_error, "Failed to create Lua state", ERROR_LEN - 1);
        S->script_state.last_error[ERROR_LEN - 1] = '\0';
        ctx->result = ESP_ERR_NO_MEM;
        xSemaphoreGive(ctx->load_sem);
        S->task_handle = NULL;
        S->script_state.current_script[0] = '\0';
        vTaskDelete(NULL);
        return;
    }

    reset_event_state();

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

    const luaL_Reg esp_dmx_lib[] = {
        {"send", l_dmx_send},
        {"read", l_dmx_read},
        {"on",   l_esp_dmx_on},
        {NULL, NULL}
    };

    const luaL_Reg esp_lib[] = {
        {"setTimeout",  l_esp_set_timeout},
        {"setInterval", l_esp_set_interval},
        {"clearTimer",  l_esp_clear_timer},
        {NULL, NULL}
    };
    lua_newtable(L);                          // [esp]
    luaL_setfuncs(L, esp_lib, 0);             // [esp]
    lua_newtable(L);                          // [esp, dmx_tbl]
    luaL_setfuncs(L, esp_dmx_lib, 0);         // [esp, dmx_tbl]
    lua_pushvalue(L, -1);                     // [esp, dmx_tbl, dmx_tbl]
    lua_setglobal(L, "dmx");                  // [esp, dmx_tbl]  (deprecated alias)
    lua_setfield(L, -2, "dmx");               // [esp]           (esp.dmx = dmx_tbl)
    lua_setglobal(L, "esp");                  // []

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
        const char *error = luaL_tolstring(L, -1, NULL);
        ESP_LOGE(TAG, "Lua load error: %s", error);
        strncpy(S->script_state.last_error, error, ERROR_LEN - 1);
        S->script_state.last_error[ERROR_LEN - 1] = '\0';
        lua_pop(L, 1);
        ctx->result = ESP_OK;
        xSemaphoreGive(ctx->load_sem);
    } else {
        strncpy(S->script_state.current_script, ctx->filename ? ctx->filename : "---", C_SCRIPT_LEN - 1);
        S->script_state.current_script[C_SCRIPT_LEN - 1] = '\0';
        S->should_stop = false;
        S->script_state.last_error[0] = '\0';
        S->last_read_universe = EMPTY;
        S->script_state.run_state = LUA_STATE_RUNNING;

        ctx->result = ESP_OK;
        xSemaphoreGive(ctx->load_sem);

        // Execute the script
        status = lua_pcall(L, 0, LUA_MULTRET, 0);
        if (status != LUA_OK) {
            if (lua_islightuserdata(L, -1) && lua_touserdata(L, -1) == (void *)KILLED_SENTINEL) {
                ESP_LOGI(TAG, "Script was killed");
                S->script_state.last_error[0] = '\0';
            } else {
                const char *error = luaL_tolstring(L, -1, NULL);
                ESP_LOGE(TAG, "Lua runtime error: %s", error);
                strncpy(S->script_state.last_error, error, ERROR_LEN - 1);
                S->script_state.last_error[ERROR_LEN - 1] = '\0';
                lua_pop(L, 1);
            }
        } else {
            S->script_state.last_error[0] = '\0';

            if (!S->should_stop && S->active_callback_total > 0) {
                run_event_loop(L);
                if (S->should_stop) {
                    ESP_LOGI(TAG, "Event loop terminated by kill");
                } else if (S->script_state.last_error[0] != '\0') {
                    ESP_LOGE(TAG, "Event loop terminated by callback error: %s", S->script_state.last_error);
                } else {
                    ESP_LOGI(TAG, "Event loop exited");
                }
            } else {
                ESP_LOGI(TAG, "Script finished");
            }
        }
    }

    reset_event_state();
    lua_close(L);
    xSemaphoreTake(S->dmx_data_mutex, portMAX_DELAY);
    S->L = NULL;
    S->task_handle = NULL;
    S->script_state.current_script[0] = '\0';
    S->last_read_universe = EMPTY;
    S->script_state.run_state = LUA_STATE_IDLE;
    xSemaphoreGive(S->dmx_data_mutex);
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
    if (S->event_queue) {
        lua_event_t evt = { .type = EVT_SHUTDOWN, .universe = 0 };
        xQueueSend(S->event_queue, &evt, 0);
    }
    xTaskAbortDelay(task_handle);
    
    // Wait a bit for it to stop gracefully
    int timeout = 100; // 1 second
    while (S->task_handle != NULL && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    task_handle = S->task_handle;

    if (task_handle != NULL) {
        ESP_LOGW(TAG, "Script was forcibly killed");
        xSemaphoreTake(S->dmx_data_mutex, portMAX_DELAY);
        strncpy(S->script_state.last_error, "Script was forcibly killed", ERROR_LEN - 1);
        S->script_state.last_error[ERROR_LEN - 1] = '\0';
        vTaskDelete(task_handle);
        reset_event_state();
        S->L = NULL;
        S->task_handle = NULL;
        S->script_state.current_script[0] = '\0';
        S->last_read_universe = EMPTY;
        S->script_state.run_state = LUA_STATE_IDLE;
        xSemaphoreGive(S->dmx_data_mutex);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

bool lua_interpreter_is_running(void) {
    return S && S->task_handle != NULL;
}

#define LUA_LIST_FILENAME_LEN 48

esp_err_t lua_interpreter_list_scripts(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_send_chunk(req, (char*)&(S->script_state), sizeof(script_state_t));

    DIR *dir = opendir("/user");
    if (dir != NULL) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ends_with(ent->d_name, ".lua") || ends_with(ent->d_name, ".luac")) {
                httpd_resp_send_chunk(req, ent->d_name, LUA_LIST_FILENAME_LEN);
            }
        }
        closedir(dir);
    }

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}
