# Adding MQTT support to the firmware

Forward-looking design note. The Lua interpreter already exposes a generic
event loop (`lua_interpreter.c`) driven by a single FreeRTOS queue,
per-source callback arrays, and a shared `active_callback_total` counter.
This document captures how to slot MQTT into that structure without
disturbing the DMX path or the timer subsystem.

MQTT support has **two surfaces** that share one MQTT client:

1. **Built-in DMX routing** (no Lua). If `mqtt_broker_uri` is configured,
   the device subscribes to two fixed topics and routes each incoming
   message into `route_dmx_data()`:
   - `dmx/universe/<N>/<start>` — payload is a **comma-separated decimal
     byte list** (`"255,0,0,0,128"`), patched into channels
     `<start> .. <start>+len-1`. The other channels in the universe keep
     their previous MQTT-set values.
2. **Lua scripting** (`mqtt.*`). Scripts can publish and subscribe to
   arbitrary topics from inside the event loop, just like they use
   `esp.dmx.on`. `mqtt.c` forwards every message to the interpreter
   verbatim (raw bytes + topic); the script decodes whatever format it
   chose.

The connection is opened once at boot from `mqtt_broker_uri` and stays up
across script reloads.

The built-in topic cover wire shapes that fit Home Assistant
naturally: HA automations that touch a small range of channels publish
to `dmx/universe/<N>/<start>` with a CSV
payload. If a user wants to push a whole universe in CSV form, they
publish to `dmx/universe/1/1` — start at channel 1, list every byte.

## How MQTT subscriptions actually work on the wire

MQTT is broker-mediated pub/sub. The broker only forwards messages on
topics the client has explicitly subscribed to:

1. Client connects (TCP + CONNECT packet).
2. Client sends `SUBSCRIBE topic=…, qos=…` → broker records this in the
   client's session.
3. Some other client `PUBLISH topic=…, payload=…` → broker matches the
   topic against all subscriptions and forwards to every subscriber.
4. Client sends `UNSUBSCRIBE topic=…` → broker stops forwarding that one.

Wildcards (`+` for one level, `#` for tail) are matched by the broker.
`+` matches exactly one level — so `dmx/universe/+/+` catches `dmx/universe/5/10`
but NOT `dmx/universe/5`.

On disconnect, with a "clean session" the broker forgets subscriptions
and they must be re-issued on reconnect. The ESP-IDF MQTT client
exposes an `MQTT_EVENT_CONNECTED` event we hook for that.

## Configuration

One new field in `app_config_t` plus one bit in `enabled_modules`. That's
the entire user-facing config surface.

```c
// app_config.h
#define MQTT_BROKER_URI_LEN 256

typedef struct __attribute__((packed)) {
    // ...existing fields...
    uint32_t enabled_modules;
    // ...
    char mqtt_broker_uri[MQTT_BROKER_URI_LEN];  // "" = disabled
    // ...
} app_config_t;
```

`mqtt_broker_uri` is a standard ESP-MQTT URI:
`mqtt://user:pass@host:1883`, `mqtts://…`, `ws://…`, etc. Empty string =
MQTT disabled regardless of the `MOD_EN_MQTT` bit. Credentials and TLS
parameters ride in the URI, so no separate fields are needed.

The `reserved[]` padding in `app_config_t` shrinks (or the field is
appended after `reserved[]` with the existing layout untouched — choose
whichever keeps NVS migration trivial).

### Module flag

```c
// modules.h
#define MOD_EN_MQTT          (1<<18)

#ifdef MQTT_SUPPORTED                  // set from Kconfig / build flag
    #define _BIT_MQTT_SUPP    MOD_EN_MQTT
#else
    #define _BIT_MQTT_SUPP    0
#endif
```

Add `_BIT_MQTT_SUPP` to `SUPPORTED_MODULES`. Add `MOD_EN_MQTT` to
`DEFAULT_ENABLED_MODULES` only if you want it on by default; otherwise
the user enables it from the dashboard.

### Firmware support flag in `/config` meta

The dashboard reads `meta.supported` (a `SUPPORTED_MODULES` bitmask) from
the `http_get_config_handler` response in `web_server.c` to know which
features the running firmware knows about. `_BIT_MQTT_SUPP` flows through
the existing path — no extra field. If the bit is set, the dashboard
shows the MQTT tab; otherwise it hides it.

### Save flow

`http_put_config_handler` already reboots the device on every config
write. MQTT inherits that — there is no live re-configure path. Boot
reads `mqtt_broker_uri`; if non-empty and `MOD_EN_MQTT` is set, the
MQTT client starts and subscribes to `dmx/universe/+`.

## File layout — `mqtt.c` / `mqtt.h`

All MQTT logic lives in a new `main/src/mqtt.c` + `mqtt.h`, mirroring the
shape of `artnet_server`, `ws2812`, `ambitful_ble`, etc. `main.c` only
calls one entry point; the module checks its own enable flag and
returns early if disabled:

```c
// main.c — drop-in alongside the existing module starts
LOG_ON_ERROR(start_mqtt(&app_config), TAG, "start_mqtt failed");
```

```c
// mqtt.h
#ifndef MQTT_H
#define MQTT_H

#include "app_config.h"
#include "esp_err.h"
#include "router.h"

typedef enum {
    MQTT_STATUS_DISABLED,
    MQTT_STATUS_CONNECTING,
    MQTT_STATUS_CONNECTED,
    MQTT_STATUS_DISCONNECTED,
} mqtt_status_t;

/**
 * @brief Start the MQTT client.
 *
 * No-op (returns ESP_OK) if MOD_EN_MQTT is off or mqtt_broker_uri is empty.
 * On success: registers the event handler, opens the connection, and
 * subscribes to "dmx/universe/+" and "dmx/universe/+/+" once connected.
 */
esp_err_t start_mqtt(app_config_t *config);

/** Connection state for /status reporting and Lua. */
mqtt_status_t mqtt_get_status(void);
const char   *mqtt_status_str(void);  // "connected" / "connecting" / ...

/** Publish — thin wrapper around esp_mqtt_client_publish. */
int mqtt_publish(const char *topic, const void *payload, size_t len,
                 int qos, bool retain);

/**
 * Subscribe / unsubscribe helpers used by Lua scripts.
 *
 * Pure pass-throughs to esp_mqtt_client_*, with ONE guard:
 * mqtt_unsubscribe()
 * "dmx/universe/+/+" while built-in DMX routing is on.
 *
 * Bookkeeping of "which Lua patterns are active" lives in
 * lua_interpreter.c, not here.
 */
esp_err_t mqtt_subscribe(const char *pattern, int qos);
esp_err_t mqtt_unsubscribe(const char *pattern);

/**
 * Hook the Lua interpreter installs at boot. Called from the MQTT event
 * task on every MQTT_EVENT_DATA, AFTER built-in DMX routing has run.
 * Payload is forwarded as-is — the interpreter (and the Lua script
 * behind it) decides what the bytes mean. NULL = interpreter disabled.
 */
typedef void (*mqtt_data_cb_t)(const char *topic, size_t topic_len,
                               const uint8_t *payload, size_t payload_len);
void mqtt_set_data_cb(mqtt_data_cb_t cb);

/**
 * Hook the Lua interpreter installs to re-issue its subscriptions on
 * (re)connect. Called from MQTT_EVENT_CONNECTED, AFTER mqtt.c has
 * re-subscribed to its own built-in topics. NULL = no Lua subs to re-issue.
 */
typedef void (*mqtt_connected_cb_t)(void);
void mqtt_set_connected_cb(mqtt_connected_cb_t cb);

#endif // MQTT_H
```

The two hooks (`mqtt_set_data_cb`, `mqtt_set_connected_cb`) keep `mqtt.c`
ignorant of Lua internals. **All Lua-subscription bookkeeping lives in
`lua_interpreter.c`** — the `mqtt_cb[]` array, the pending payload slot,
the `EVT_MQTT` posting, and the on-reconnect re-subscribe loop.

### `mqtt.c` skeleton

```c
// mqtt.c
#define MAX_MQTT_UNIVERSES 4         // cap on dynamically-tracked universes

typedef struct {
    uint16_t universe;               // 0 = free
    uint8_t *buf;                    // 512 bytes; lazy calloc on first write
} mqtt_universe_state_t;

static esp_mqtt_client_handle_t   client;
static app_config_t              *app_config;
static volatile mqtt_status_t     status = MQTT_STATUS_DISABLED;
static mqtt_data_cb_t             data_cb;       // NULL until Lua registers
static mqtt_connected_cb_t        connected_cb;
static mqtt_universe_state_t      universes[MAX_MQTT_UNIVERSES];

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t event_id, void *event_data);

esp_err_t start_mqtt(app_config_t *config) {
    app_config = config;
    if (!(config->enabled_modules & MOD_EN_MQTT) || config->mqtt_broker_uri[0] == '\0') {
        return ESP_OK;
    }

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = config->mqtt_broker_uri,
    };
    client = esp_mqtt_client_init(&cfg);
    if (!client) return ESP_FAIL;
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, event_handler, NULL);
    status = MQTT_STATUS_CONNECTING;
    return esp_mqtt_client_start(client);
}

mqtt_status_t mqtt_get_status(void) { return status; }

const char *mqtt_status_str(void) {
    switch (status) {
        case MQTT_STATUS_CONNECTED:    return "connected";
        case MQTT_STATUS_CONNECTING:   return "connecting";
        case MQTT_STATUS_DISCONNECTED: return "disconnected";
        default:                       return "disabled";
    }
}

void mqtt_set_data_cb     (mqtt_data_cb_t cb)      { data_cb = cb; }
void mqtt_set_connected_cb(mqtt_connected_cb_t cb) { connected_cb = cb; }

// Look up (or lazily allocate) the per-universe scratch buffer.
// Returns NULL if MAX_MQTT_UNIVERSES is exhausted or calloc fails.
static uint8_t *get_universe_buf(uint16_t universe) {
    int empty = -1;
    for (int i = 0; i < MAX_MQTT_UNIVERSES; i++) {
        if (universes[i].universe == universe && universes[i].buf) return universes[i].buf;
        if (universes[i].universe == 0 && empty == -1) empty = i;
    }
    if (empty < 0) return NULL;
    universes[empty].buf = calloc(DMX_LEN, 1);
    if (!universes[empty].buf) return NULL;
    universes[empty].universe = universe;
    return universes[empty].buf;
}
```

The event handler, the two built-in routes, and the unsubscribe guard
all live in `mqtt.c` — details below.

### `router.h`

```c
typedef enum {
    DATA_SOURCE_ARTNET,
    DATA_SOURCE_LUA,
    DATA_SOURCE_LUA_DEBUG,
    DATA_SOURCE_MQTT,        // <-- new
    // ...
} dmx_data_source_t;
```

## Status reporting

`/status` in `web_server.c` adds one field, reading from `mqtt_status_str()`:

```c
// in http_get_status_handler:
"\"mqtt\":\"%s\","   // mqtt_status_str()
```

The dashboard's MQTT tab polls `/status` the same way other tabs do and
renders the field. `"disabled"` covers both the module bit being off and
an empty URI.

## Event-loop integration

The existing event loop knows two event kinds: `EVT_DMX` and
`EVT_SHUTDOWN`. Add a third:

```c
typedef enum {
    EVT_DMX,
    EVT_MQTT,
    EVT_SHUTDOWN
} lua_event_type_t;

typedef struct {
    lua_event_type_t type;
    uint16_t         universe;     // EVT_DMX only; EVT_MQTT ignores it
} lua_event_t;
```

No per-slot index — the Lua side has a single global pending payload
(see next section), so `EVT_MQTT` carries no extra data.

## Lua-side state — one global pending slot

Per-slot buffering doesn't earn its complexity here. Scripts that care
about ordering can publish to per-topic state in Lua. The newest-wins
coalescing already used for DMX is the right model: if messages arrive
faster than the script can dispatch, the old one is overwritten by the
newest and that is fine.

```c
#define MAX_MQTT_CB     4
#define MQTT_TOPIC_LEN  64
#define MQTT_PAYLOAD_MAX DMX_LEN

typedef struct {
    char    pattern[MQTT_TOPIC_LEN]; // subscription pattern (with + and #)
    int     lua_ref;                 // LUA_NOREF = free slot
} lua_mqtt_cb_t;

// Add to lua_interpreter_state_t:
SemaphoreHandle_t mqtt_data_mutex;       // protects the pending payload below
lua_mqtt_cb_t     mqtt_cb[MAX_MQTT_CB];

// ONE pending message for the whole Lua MQTT surface:
char     mqtt_pending_topic[MQTT_TOPIC_LEN];
uint8_t  mqtt_pending_payload[MQTT_PAYLOAD_MAX];
uint16_t mqtt_pending_payload_len;
bool     mqtt_has_pending;
```

When dispatching, the loop matches the pending topic against every
registered pattern and calls each handler that matches with the same
payload+topic. The buffer is then cleared.

## Subscribe / unsubscribe at the broker

`mqtt.c` exposes two thin helpers:

- `mqtt_subscribe(pattern, qos)` — direct `esp_mqtt_client_subscribe`.
- `mqtt_unsubscribe(pattern)` — `esp_mqtt_client_unsubscribe`, with
  one guard: **refuses to drop `dmx/universe/+/+`**
  Those are mqtt.c's own subscription; yanking it would break the
  scriptless DMX path.

`mqtt.c` does NOT maintain a list of Lua-side patterns. That list lives
in `lua_interpreter.c` (`S->mqtt_cb[]`). On `MQTT_EVENT_CONNECTED`,
`mqtt.c` re-issues its own two built-in subscriptions and then calls
`connected_cb` (the hook installed by `lua_interpreter.c`), which walks
`S->mqtt_cb[]` and re-subscribes everything the script asked for.

`esp-mqtt` can also be configured for persistent sessions; explicit
re-subscribe on connect is the robust default and what we do.

## Producer side — MQTT client event handler (in `mqtt.c`)

The ESP-IDF MQTT client emits events on its own task. The handler does
two independent things on `MQTT_EVENT_DATA`: route to DMX if the topic
matches one of the two built-in filters, then hand the message off to
the Lua interpreter (if one has registered a callback). `mqtt.c` knows
nothing about `S`, `mqtt_cb[]`, or the event queue.

```c
// mqtt.c
static void route_full_frame(uint16_t universe, const uint8_t *data, size_t len) {
    uint8_t *buf = get_universe_buf(universe);
    size_t   n   = len > DMX_LEN ? DMX_LEN : len;
    if (buf) {
        memcpy(buf, data, n);
        if (n < DMX_LEN) memset(buf + n, 0, DMX_LEN - n);  // raw frame = authoritative
        route_dmx_data(DATA_SOURCE_MQTT, universe, buf, DMX_LEN);
    } else {
        // No scratch slot: send the payload as-is. Subsequent partial
        // writes for this universe won't have prior state to merge with.
        route_dmx_data(DATA_SOURCE_MQTT, universe, data, n);
    }
}

// Parse a CSV decimal payload ("255,0,128,..") into the scratch buffer
// starting at start_ch (1-indexed). Out-of-range / non-numeric tokens
// are clamped to 0..255 / skipped. Returns the number of bytes written.
static size_t patch_csv_into_buf(uint8_t *buf, uint16_t start_ch,
                                 const char *payload, size_t payload_len);

static void route_partial(uint16_t universe, uint16_t start_ch,
                          const char *data, size_t len) {
    if (start_ch < 1 || start_ch > DMX_LEN) return;
    uint8_t *buf = get_universe_buf(universe);
    if (!buf) return;                              // no slot; silently drop
    size_t written = patch_csv_into_buf(buf, start_ch, data, len);
    if (!written) return;
    route_dmx_data(DATA_SOURCE_MQTT, universe, buf, DMX_LEN);
}

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t event_id, void *event_data) {
    if (event_id == MQTT_EVENT_CONNECTED) {
        status = MQTT_STATUS_CONNECTED;
        esp_mqtt_client_subscribe(client, "dmx/universe/+",   0);
        esp_mqtt_client_subscribe(client, "dmx/universe/+/+", 0);
        if (connected_cb) connected_cb();          // lua_interpreter re-issues its subs
        return;
    }
    if (event_id == MQTT_EVENT_DISCONNECTED) { status = MQTT_STATUS_DISCONNECTED; return; }
    if (event_id != MQTT_EVENT_DATA) return;

    esp_mqtt_event_handle_t e = event_data;

    // 1. Built-in DMX routing.
    uint16_t universe, start_ch;
    if (parse_universe_channel_topic(e->topic, e->topic_len,
                                            &universe, &start_ch)) {
        // dmx/universe/<N>/<start_ch> — CSV decimal, partial patch
        route_partial(universe, start_ch, e->data, e->data_len);
    }

    // 2. Lua: hand off to whichever callback the interpreter installed.
    //    Raw bytes go through verbatim — the script decodes whatever
    //    format it expects (raw, CSV, JSON, …).
    if (data_cb) {
        data_cb(e->topic, e->topic_len,
                (const uint8_t *)e->data, e->data_len);
    }
}
```

`parse_universe_channel_topic` accepts four segments
(`dmx/universe/<N>/<start>`). Each returns `true` only on its own
shape, so the two paths are mutually exclusive — no double-routing.

The Lua interpreter installs hook in `lua_interpreter_init`:

```c
mqtt_set_data_cb     (lua_mqtt_on_message);
mqtt_set_connected_cb(lua_mqtt_on_connected);
```

`lua_mqtt_on_message` (defined in `lua_interpreter.c`) does the
pattern-match against `S->mqtt_cb[]`, fills the single global pending
slot, and posts `EVT_MQTT`. `lua_mqtt_on_connected` re-issues every
Lua-held subscription:

```c
// lua_interpreter.c
static void lua_mqtt_on_message(const char *topic, size_t topic_len,
                                const uint8_t *payload, size_t payload_len) {
    if (!S || !S->task_handle) return;
    bool any_match = false;
    for (int i = 0; i < MAX_MQTT_CB; i++) {
        if (S->mqtt_cb[i].lua_ref == LUA_NOREF) continue;
        if (mqtt_topic_match(S->mqtt_cb[i].pattern, topic, topic_len)) {
            any_match = true; break;
        }
    }
    if (!any_match) return;

    if (xSemaphoreTake(S->mqtt_data_mutex, 0) != pdTRUE) return;  // never block the MQTT task
    size_t plen = payload_len > MQTT_PAYLOAD_MAX ? MQTT_PAYLOAD_MAX : payload_len;
    size_t tlen = topic_len   >= MQTT_TOPIC_LEN  ? MQTT_TOPIC_LEN - 1 : topic_len;
    memcpy(S->mqtt_pending_payload, payload, plen);
    memcpy(S->mqtt_pending_topic,   topic,   tlen);
    S->mqtt_pending_topic[tlen]    = '\0';
    S->mqtt_pending_payload_len    = plen;
    S->mqtt_has_pending            = true;
    xSemaphoreGive(S->mqtt_data_mutex);

    if (S->event_queue) {
        lua_event_t evt = { .type = EVT_MQTT };
        xQueueSend(S->event_queue, &evt, 0);  // non-blocking; drop on full
    }
}

static void lua_mqtt_on_connected(void) {
    if (!S) return;
    for (int i = 0; i < MAX_MQTT_CB; i++) {
        if (S->mqtt_cb[i].lua_ref != LUA_NOREF) {
            mqtt_subscribe(S->mqtt_cb[i].pattern, /*qos*/0);
        }
    }
}
```

Built-in DMX routing has no coalescing — every matched message hits
`route_dmx_data()` immediately, and the router decides what to do.
That's the right behavior for DMX output (state, not stream).

## Dispatcher in the event loop

One `EVT_MQTT` may fan out to multiple Lua callbacks (any pattern that
matches the pending topic). Snapshot the pending payload under the
mutex, then call each matching handler with the same data:

```c
static bool dispatch_mqtt_event(lua_State *L) {
    char    topic[MQTT_TOPIC_LEN];
    uint8_t payload[MQTT_PAYLOAD_MAX];
    size_t  payload_len = 0;
    bool    have = false;

    xSemaphoreTake(S->mqtt_data_mutex, portMAX_DELAY);
    if (S->mqtt_has_pending) {
        memcpy(topic,   S->mqtt_pending_topic, MQTT_TOPIC_LEN);
        memcpy(payload, S->mqtt_pending_payload, S->mqtt_pending_payload_len);
        payload_len = S->mqtt_pending_payload_len;
        S->mqtt_has_pending = false;
        have = true;
    }
    xSemaphoreGive(S->mqtt_data_mutex);

    if (!have) return true;

    for (int i = 0; i < MAX_MQTT_CB; i++) {
        int ref = S->mqtt_cb[i].lua_ref;
        if (ref == LUA_NOREF) continue;
        if (!mqtt_topic_match(S->mqtt_cb[i].pattern, topic, strlen(topic))) continue;

        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);            // [fn]
        lua_pushlstring(L, (const char *)payload, payload_len);
        lua_pushstring(L, topic);
        int status = lua_pcall(L, 2, 0, 0);
        if (status != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            if (err) {
                strncpy(S->last_error, err, ERROR_LEN - 1);
                S->last_error[ERROR_LEN - 1] = '\0';
            }
            lua_pop(L, 1);
            return false;
        }
    }
    lua_gc(L, LUA_GCSTEP, 0);
    return true;
}
```

And in `run_event_loop`:

```c
case EVT_MQTT:
    if (!dispatch_mqtt_event(L)) break;
    break;
```

## Public Lua surface

```lua
esp.mqtt.publish(topic, payload)
esp.mqtt.publish(topic, payload, {qos=0, retain=false})

esp.mqtt.on(topic, fn)                    -- topic may contain '+' and '#'
esp.mqtt.on(topic, fn, {qos=1})           -- with options
esp.mqtt.on(topic, nil)                   -- unsubscribe (refused for dmx/universe/+
                                      --              when built-in routing is on)

-- Callback signature
function(payload, topic)              -- topic is the *concrete* topic, not the pattern
    ...
end
```

## `mqtt.on` registration

Structurally identical to `l_esp_dmx_on` after the simplification
(release-then-maybe-insert). All broker traffic flows through
`mqtt_subscribe` / `mqtt_unsubscribe` (defined in `mqtt.c`),
which is also where the `dmx/universe/+` unsubscribe guard lives:

```c
// lua_interpreter.c
static int l_esp_mqtt_on(lua_State *L) {
    size_t plen;
    const char *pattern = luaL_checklstring(L, 1, &plen);
    int fn_type = lua_type(L, 2);

    if (plen >= MQTT_TOPIC_LEN)
        return luaL_error(L, "mqtt.on: topic too long (max %d)", MQTT_TOPIC_LEN - 1);
    if (fn_type != LUA_TFUNCTION && fn_type != LUA_TNIL)
        return luaL_error(L, "mqtt.on: handler must be function or nil");

    int slot = -1, empty = -1;
    for (int i = 0; i < MAX_MQTT_CB; i++) {
        if (S->mqtt_cb[i].lua_ref != LUA_NOREF
            && strcmp(S->mqtt_cb[i].pattern, pattern) == 0) { slot = i; break; }
        if (S->mqtt_cb[i].lua_ref == LUA_NOREF && empty == -1) empty = i;
    }

    if (slot >= 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, S->mqtt_cb[slot].lua_ref);
        S->mqtt_cb[slot].lua_ref = LUA_NOREF;
        S->active_callback_total--;
        mqtt_unsubscribe(S->mqtt_cb[slot].pattern);  // guard inside mqtt.c
    }

    if (fn_type == LUA_TNIL) return 0;

    if (slot < 0) slot = empty;
    if (slot < 0) return luaL_error(L, "mqtt.on: no free slot (max %d)", MAX_MQTT_CB);

    lua_pushvalue(L, 2);
    S->mqtt_cb[slot].lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    strncpy(S->mqtt_cb[slot].pattern, pattern, MQTT_TOPIC_LEN - 1);
    S->mqtt_cb[slot].pattern[MQTT_TOPIC_LEN - 1] = '\0';
    S->active_callback_total++;

    mqtt_subscribe(pattern, /*qos*/0);
    return 0;
}
```

The `mqtt_cb[]` array is the source of truth for **Lua-side handlers**;
the `lua_subs[]` table inside `mqtt.c` is the source of truth for the
**broker-side subscriptions** Lua opened (so reconnect can re-issue
them without touching the Lua state).

## `mqtt.publish`

```c
// lua_interpreter.c
static int l_esp_mqtt_publish(lua_State *L) {
    const char *topic = luaL_checkstring(L, 1);
    size_t plen;
    const char *payload = luaL_checklstring(L, 2, &plen);
    int qos = 0;
    bool retain = false;
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "qos");    if (!lua_isnil(L, -1)) qos    = lua_tointeger(L, -1); lua_pop(L, 1);
        lua_getfield(L, 3, "retain"); if (!lua_isnil(L, -1)) retain = lua_toboolean(L, -1); lua_pop(L, 1);
    }
    if (mqtt_get_status() != MQTT_STATUS_CONNECTED)
        return luaL_error(L, "mqtt.publish: not connected");
    int msg_id = mqtt_publish(topic, payload, plen, qos, retain);
    lua_pushinteger(L, msg_id);
    return 1;
}
```

## Connection lifecycle

`main.c` just calls `start_mqtt(&app_config)` alongside the other module
starts. `mqtt.c` does the enable-flag + URI check, registers the event
handler, and opens the connection. Built-in routing subscribes to
`dmx/universe/+/+` from inside
`MQTT_EVENT_CONNECTED` — see the event handler above.

Config changes write NVS and reboot — handled by the existing
`http_put_config_handler`. No live re-configure path.

The Lua-side callback table resets per script (via `reset_event_state`),
so Lua subscriptions are torn down + re-issued to the broker per script.
The MQTT client itself, the two built-in subscriptions, and the
per-universe scratch buffers are owned by `mqtt.c` and not affected by
script reloads.

## Registration in `lua_task`

Add the sub-table after `esp.dmx`:

```c
const luaL_Reg esp_mqtt_lib[] = {
    {"publish", l_esp_mqtt_publish},
    {"on",      l_esp_mqtt_on},
    {"status",  l_esp_mqtt_status},
    {NULL, NULL}
};
lua_newtable(L);
luaL_setfuncs(L, esp_mqtt_lib, 0);
lua_setfield(L, -2, "mqtt");   // stack: [esp]
```

## Reset and shutdown

`reset_event_state()` already zeroes `active_callback_total` and clears
the universe and timer slots. Add the MQTT slots — broker unsubscribe
goes through `mqtt_unsubscribe`, which silently no-ops on
`dmx/universe/+` while built-in routing is on:

```c
// lua_interpreter.c — inside reset_event_state()
for (int i = 0; i < MAX_MQTT_CB; i++) {
    if (S->mqtt_cb[i].lua_ref != LUA_NOREF) {
        mqtt_unsubscribe(S->mqtt_cb[i].pattern);
        luaL_unref(L, LUA_REGISTRYINDEX, S->mqtt_cb[i].lua_ref);
        S->mqtt_cb[i].lua_ref = LUA_NOREF;
    }
}
S->mqtt_has_pending = false;
```

The MQTT client itself stays up across script reloads — connection
state is C-owned in `mqtt.c` and survives.

## Memory budget

| Item | Approx size | Count | Total |
|---|---|---|---|
| `lua_mqtt_cb_t` | `64 + 4` ≈ 68 B | `MAX_MQTT_CB = 4` | 272 B |
| Global pending payload+topic | `64 + 512 + 4 + 1` ≈ 580 B | 1 | 580 B |
| `mqtt_broker_uri` | 256 B | 1 (in `app_config_t`) | 256 B |
| Per-universe scratch (lazy heap) | 512 B | up to `MAX_MQTT_UNIVERSES = 4` | up to 2 KB |
| `mqtt_universe_state_t` table | `4` ≈ 4 B | 4 | 16 B |
| `event_queue` entries (no growth) | 4–8 B | 8 | 32–64 B |
| `esp_mqtt_client` | (ESP-IDF) | 1 | ~4 KB |

Total: ~7 KB resident in the worst case (all four universes touched).
Comfortable on ESP32 (520 KB SRAM). The per-universe buffers are heap-
allocated on first use, so devices that never receive MQTT-driven DMX
pay nothing for them.

## Wildcard matching and topic parsing

For Lua subscription patterns we need a generic MQTT topic matcher
supporting `+` (single level) and `#` (multi level):

```c
static bool mqtt_topic_match(const char *pattern, const char *topic, size_t topic_len);
```

Common, well-tested implementations exist in `esp-mqtt` examples and
elsewhere. Don't roll a buggy one — borrow.

For the two built-in routes a generic matcher is overkill — they're
fixed shapes, so use specific parsers that return both the match
verdict AND the captured numbers in one pass:

```c
// "dmx/universe/<N>"          → true + universe
static bool parse_universe_topic(const char *topic, size_t topic_len,
                                 uint16_t *universe);
// "dmx/universe/<N>/<start>"  → true + universe + start_ch
static bool parse_universe_channel_topic(const char *topic, size_t topic_len,
                                         uint16_t *universe, uint16_t *start_ch);
```

These reject anything that doesn't match the exact segment count, so
`dmx/universe/5/10/extra` and `dmx/universe` both fall through to the
Lua-only path.

## Edge cases worth noting

- **Universe number 0** in `dmx/universe/0/N` —
  whether to accept depends on what `route_dmx_data` does with universe
  0 today (Art-Net allows it). Treat consistently.
- **CSV payload longer than the universe**: clamp at channel 512;
  ignore trailing values.
- **CSV value out of range** (negative, > 255): clamp to `[0, 255]`.
- **Non-numeric / malformed CSV token**: skip the token but advance the
  channel pointer (or stop — implementation choice; skipping is more
  forgiving).
- **Empty payload** on partial write: no-op (don't re-route the buffer).
- **`MAX_MQTT_UNIVERSES` exhausted**: silently drop further partial
  writes for new universes. Raw-frame writes still work but skip the
  buffer-merging step. Log once.
- **First partial write to a universe** before any raw frame: starts
  from a zeroed buffer, so untouched channels go out as 0. Document
  this — users who want non-zero baselines should send a raw frame
  first.

## Open questions for implementation time

1. **QoS handling on Lua subscribe**: pass through to broker, or always
   0? Probably pass through (`mqtt.on(topic, fn, {qos=1})`).
2. **Retained messages**: surfacing them via the same `fn(payload, topic)`
   is natural, but scripts may want to know "this is retained". Add a
   third arg? `function(payload, topic, retained) end`.
3. **MQTT support gating**: `MQTT_SUPPORTED` from a Kconfig option vs a
   raw build flag — pick whichever matches how `LUA_INTERPRETER` and
   `ARTNET` are gated today.

## Summary checklist

When implementing:

**Shared infrastructure**
- [ ] Add `MOD_EN_MQTT` to `modules.h`, gated `_BIT_MQTT_SUPP` from a
      `MQTT_SUPPORTED` build flag, fold into `SUPPORTED_MODULES`.
- [ ] Add `char mqtt_broker_uri[256]` to `app_config_t` (with NVS
      version bump or appended after `reserved[]`).
- [ ] Add `DATA_SOURCE_MQTT` to the `dmx_data_source_t` enum in
      `router.h`.

**`mqtt.c` / `mqtt.h` (new module)**
- [ ] Public API: `start_mqtt`, `mqtt_get_status`, `mqtt_status_str`,
      `mqtt_publish`, `mqtt_subscribe`, `mqtt_unsubscribe`,
      `mqtt_set_data_cb`, `mqtt_set_connected_cb`.
- [ ] Borrow a tested `mqtt_topic_match` (`+` / `#`). Implement the two
      specific parsers `parse_universe_topic` and
      `parse_universe_channel_topic`.
- [ ] Implement `patch_csv_into_buf` (CSV decimal → buffer slice,
      clamp + skip-on-error).
- [ ] Per-universe scratch table `universes[MAX_MQTT_UNIVERSES]` with
      lazy `calloc(DMX_LEN, 1)` via `get_universe_buf`.
- [ ] Event handler: CONNECTED re-subscribes `dmx/universe/+` and
      `dmx/universe/+/+` then calls `connected_cb`; DATA dispatches
      to raw / partial routes then invokes `data_cb`; DISCONNECTED
      updates `status`.
- [ ] `mqtt_unsubscribe` refuses to drop either built-in topic while
      DMX routing is on.
- [ ] `start_mqtt`: no-op if `MOD_EN_MQTT` is off or URI is empty;
      otherwise init + register + start the client.

**main.c / web_server.c wiring**
- [ ] Call `start_mqtt(&app_config)` from `main.c` next to the other
      module starts.
- [ ] Extend `http_get_status_handler` JSON with `"mqtt":<state>` from
      `mqtt_status_str()`.
- [ ] Dashboard MQTT tab: broker URI field + enable toggle + status
      readout fed by `/status`. Hide tab if `meta.supported &
      MOD_EN_MQTT` is 0.

**Lua surface (in `lua_interpreter.c`)**
- [ ] Add `EVT_MQTT` to the event enum.
- [ ] Add `lua_mqtt_cb_t mqtt_cb[]`, the global pending payload+topic,
      and the mutex to `lua_interpreter_state_t`.
- [ ] Initialize `mqtt_cb[]` (LUA_NOREF in every slot) in
      `ensure_lua_state`; call `mqtt_set_data_cb(lua_mqtt_on_message)`
      and `mqtt_set_connected_cb(lua_mqtt_on_connected)` in
      `lua_interpreter_init`.
- [ ] Implement `lua_mqtt_on_message` (producer hook: pattern-match +
      fill pending slot + post `EVT_MQTT`).
- [ ] Implement `lua_mqtt_on_connected` (re-issue every active Lua
      subscription via `mqtt_subscribe`).
- [ ] Implement `dispatch_mqtt_event` (snapshot pending, fan out over
      matching patterns).
- [ ] Add `case EVT_MQTT:` in `run_event_loop`.
- [ ] Implement `l_esp_mqtt_publish`, `l_esp_mqtt_on`,
      `l_esp_mqtt_status` — all going through the `mqtt.h` API.
- [ ] Register `mqtt` sub-table in `lua_task`.
- [ ] Update `reset_event_state` to call `mqtt_unsubscribe` for
      each Lua-held topic and clear slots.

**Polish**
- [ ] Update `docs/` if the public Lua API surface changes between
      drafting and implementation.
