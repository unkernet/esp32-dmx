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

/** Publish — thin wrapper around esp_mqtt_client_publish. */
int mqtt_publish(const char *topic, const void *payload, size_t len,
                 int qos, bool retain);

/**
 * Subscribe / unsubscribe helpers used by Lua scripts.
 *
 * Pure pass-throughs to esp_mqtt_client_*, with ONE guard:
 * mqtt_unsubscribe() refuses to drop "dmx/universe/+/+".
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

bool mqtt_topic_match(const char *pattern, const char *topic, size_t topic_len);

#endif // MQTT_H
