#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mqtt.h"
#include <mqtt_client.h>
#include <esp_log.h>
#include "modules.h"
#include "router.h"
#include <mdns.h>
#include <esp_mac.h>

static const char *TAG = "MQTT";

#define MAX_MQTT_UNIVERSES 4         // cap on dynamically-tracked universes

typedef struct {
    uint16_t universe;               // 0 = free
    uint8_t *buf;                    // 512 bytes; lazy calloc on first write
} mqtt_universe_state_t;

static esp_mqtt_client_handle_t   client;
static app_config_t              *app_cfg;
static volatile mqtt_status_t     status = MQTT_STATUS_DISABLED;
static mqtt_data_cb_t             data_cb = NULL;
static mqtt_connected_cb_t        connected_cb = NULL;
static mqtt_universe_state_t      universes[MAX_MQTT_UNIVERSES];
static char                       mqtt_uri_resolved[MQTT_BROKER_URI_LEN];
static bool                       discovery_done = false;

// --- Home Assistant Discovery ---

static void publish_ha_discovery() {
    if (discovery_done) return;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char dev_id[16];
    snprintf(dev_id, sizeof(dev_id), "esp-dmx-%02x%02x", mac[4], mac[5]);

    char topic[64];
    char payload[320];

    // Discovery for a generic "light" representing the whole device or first universe
    // This is a simplified example; real DMX devices usually have many entities.
    snprintf(topic, sizeof(topic), "homeassistant/light/%s/config", dev_id);
    snprintf(payload, sizeof(payload),
        "{"
            "\"name\":\"ESP-DMX Controller\","
            "\"unique_id\":\"%s\","
            "\"cmd_t\":\"dmx/universe/1/1\","
            "\"schema\":\"template\","
            "\"command_on_template\":\"255,255,255\","
            "\"command_off_template\":\"0,0,0\","
            "\"device\":{"
                "\"identifiers\":[\"%s\"],"
                "\"name\":\"%s\","
                "\"model\":\"ESP-DMX\""
            "}"
        "}",
        dev_id, dev_id, dev_id
    );

    esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
    discovery_done = true;
    ESP_LOGI(TAG, "Home Assistant discovery published");
}

// --- Topic Parsing ---

bool mqtt_topic_match(const char *pattern, const char *topic, size_t topic_len) {
    if (pattern == NULL || topic == NULL) return false;

    const char *p = pattern;
    const char *t = topic;
    const char *t_end = topic + topic_len;

    while (*p && t < t_end) {
        if (*p == '+') {
            while (t < t_end && *t != '/') t++;
            p++;
        } else if (*p == '#') {
            return true;
        } else if (*p == *t) {
            p++;
            t++;
        } else {
            return false;
        }
    }

    return *p == '\0' && t == t_end;
}

static bool parse_uint16(const char **p, const char *end, uint16_t *val) {
    if (*p >= end || **p < '0' || **p > '9') return false;
    uint32_t res = 0;
    while (*p < end && **p >= '0' && **p <= '9') {
        res = res * 10 + (**p - '0');
        if (res > 65535) return false;
        (*p)++;
    }
    *val = (uint16_t)res;
    return true;
}

static bool parse_universe_channel_topic(const char *topic, size_t topic_len,
                                         uint16_t *universe, uint16_t *start_ch) {
    // Expected: dmx/universe/<N>/<start>
    const char *end = topic + topic_len;
    if (topic_len < 15 || strncmp(topic, "dmx/universe/", 13) != 0) return false;

    const char *p = topic + 13;
    if (!parse_uint16(&p, end, universe)) return false;
    if (p >= end || *p != '/') return false;

    p++; // skip '/'
    if (!parse_uint16(&p, end, start_ch)) return false;

    return p == end;
}

static size_t patch_csv_into_buf(uint8_t *buf, uint16_t start_ch,
                                 const char *payload, size_t payload_len) {
    if (start_ch < 1 || start_ch > DMX_LEN || !payload || payload_len == 0) return 0;
    
    size_t written = 0;
    uint16_t curr_ch = start_ch;
    const char *p = payload;
    const char *end = payload + payload_len;

    while (p < end && curr_ch <= DMX_LEN) {
        // Skip delimiters (comma, whitespace, etc.)
        while (p < end && !(*p >= '0' && *p <= '9')) {
            p++;
        }
        if (p >= end) break;

        uint16_t val;
        if (parse_uint16(&p, end, &val)) {
            buf[curr_ch - 1] = (val > 255) ? 255 : (uint8_t)val;
            curr_ch++;
            written++;
        }
    }

    return written;
}


static uint8_t *get_universe_buf(uint16_t universe) {
    int empty = -1;
    for (int i = 0; i < MAX_MQTT_UNIVERSES; i++) {
        if (universes[i].universe == universe && universes[i].buf) {
            return universes[i].buf;
        }
        if (universes[i].universe == 0 && empty == -1) {
            empty = i;
        }
    }
    if (empty < 0) {
        return NULL;
    }
    universes[empty].buf = calloc(DMX_LEN, 1);
    if (!universes[empty].buf) return NULL;
    universes[empty].universe = universe;
    return universes[empty].buf;
}

static void route_partial(uint16_t universe, uint16_t start_ch,
                          const char *data, size_t len) {
    if (start_ch < 1) start_ch = 1;
    if (start_ch > DMX_LEN) return;
    uint8_t *buf = get_universe_buf(universe);
    if (!buf) {
        ESP_LOGW(TAG, "No scratch buffer available for universe %d", universe);
        return;
    }
    size_t written = patch_csv_into_buf(buf, start_ch - 1, data, len);
    if (!written) {
        return;
    }
    route_dmx_data(DATA_SOURCE_MQTT, universe, buf, DMX_LEN);
}

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected");
            status = MQTT_STATUS_CONNECTED;
            // if (app_cfg->mqtt_broker_uri[0] == '\0') {
            //     memcpy(app_cfg->mqtt_broker_uri, mqtt_uri_resolved, sizeof(app_cfg->mqtt_broker_uri));
            // }
            esp_mqtt_client_subscribe(client, "dmx/universe/+/+", 0);
            if (connected_cb) {
                connected_cb();
            }
            publish_ha_discovery();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Disconnected");
            status = MQTT_STATUS_DISCONNECTED;
            break;
        case MQTT_EVENT_DATA: {
            uint16_t universe, start_ch;
            if (parse_universe_channel_topic(event->topic, event->topic_len, &universe, &start_ch)) {
                route_partial(universe, start_ch, event->data, event->data_len);
            }

            if (data_cb) {
                data_cb(event->topic, event->topic_len, (const uint8_t *)event->data, event->data_len);
            }
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            break;
    }
}

static void mqtt_discovery_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting MQTT broker discovery...");
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr("_home-assistant", "_tcp", 5000, 1, &results);
    if (err == ESP_OK && results) {
        if (results->addr) {
            char ip_str[16];
            esp_ip4_addr_t *addr = (esp_ip4_addr_t *)&results->addr->addr.u_addr.ip4;
            esp_ip4addr_ntoa(addr, ip_str, sizeof(ip_str));
            snprintf(mqtt_uri_resolved, sizeof(mqtt_uri_resolved), "mqtt://%s:1883", ip_str);
            ESP_LOGI(TAG, "Found homeassistant via mDNS: %s", mqtt_uri_resolved);
            
            esp_mqtt_client_config_t mqtt_cfg = {
                .broker.address.uri = mqtt_uri_resolved,
            };
            client = esp_mqtt_client_init(&mqtt_cfg);
            if (client) {
                esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, event_handler, NULL);
                status = MQTT_STATUS_CONNECTING;
                esp_mqtt_client_start(client);
            }
        }
        mdns_query_results_free(results);
    } else {
        ESP_LOGI(TAG, "MQTT broker not found via mDNS");
        status = MQTT_STATUS_DISABLED;
    }
    vTaskDelete(NULL);
}

esp_err_t start_mqtt(app_config_t *config) {
    app_cfg = config;
    if (!(config->enabled_modules & MOD_EN_MQTT)) {
        status = MQTT_STATUS_DISABLED;
        return ESP_OK;
    }

    if (config->mqtt_broker_uri[0] != '\0') {
        esp_mqtt_client_config_t mqtt_cfg = {
            .broker.address.uri = config->mqtt_broker_uri,
        };
        client = esp_mqtt_client_init(&mqtt_cfg);
        if (!client) return ESP_FAIL;
        esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, event_handler, NULL);
        status = MQTT_STATUS_CONNECTING;
        return esp_mqtt_client_start(client);
    } else {
        // Auto-discovery
        // xTaskCreate(mqtt_discovery_task, "mqtt_disc", 2048, NULL, 5, NULL);
        return ESP_OK;
    }
}

mqtt_status_t mqtt_get_status(void) { return status; }

int mqtt_publish(const char *topic, const void *payload, size_t len, int qos, bool retain) {
    if (status != MQTT_STATUS_CONNECTED) return -1;
    return esp_mqtt_client_publish(client, topic, payload, len, qos, retain);
}

esp_err_t mqtt_subscribe(const char *pattern, int qos) {
    if (status != MQTT_STATUS_CONNECTED) return ESP_ERR_INVALID_STATE;
    return esp_mqtt_client_subscribe(client, pattern, qos) >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_unsubscribe(const char *pattern) {
    if (status != MQTT_STATUS_CONNECTED) return ESP_ERR_INVALID_STATE;
    if (strcmp(pattern, "dmx/universe/+/+") == 0) {
        return ESP_OK; // Refuse to unsubscribe from built-in topics
    }
    return esp_mqtt_client_unsubscribe(client, pattern) >= 0 ? ESP_OK : ESP_FAIL;
}

void mqtt_set_data_cb(mqtt_data_cb_t cb) { data_cb = cb; }
void mqtt_set_connected_cb(mqtt_connected_cb_t cb) { connected_cb = cb; }
