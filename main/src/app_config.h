/**
 * @file app_config.h
 * @brief Global application configuration structures and definitions.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/** @brief Maximum length for SSID strings */
#define MAX_SSID_LEN 32
/** @brief Maximum length for password strings */
#define MAX_PASSWORD_LEN 64

/** @brief WiFi connection states */
typedef enum {
    WIFI_STATE_STA_CONNECTING,
    WIFI_STATE_STA_CONNECTED,
    WIFI_STATE_STA_CONNECTED_MANUAL,
    WIFI_STATE_AP_RUNNING,
    WIFI_STATE_WAIT_RECONNECT,
} wifi_state_t;

/**
 * @brief Application configuration structure
 */

typedef struct __attribute__((packed)) {
    char ssid[MAX_SSID_LEN + 1];
    char password[MAX_PASSWORD_LEN + 1];
    uint8_t dhcp_enabled;
    uint32_t ip;
    uint8_t netmask_len;
} wifi_sta_settings_t;

typedef struct __attribute__((packed)) {
    char ssid[MAX_SSID_LEN + 1];
    char password[MAX_PASSWORD_LEN + 1];
} wifi_ap_settings_t;

typedef struct __attribute__((packed)) {
    wifi_sta_settings_t sta;
    wifi_ap_settings_t ap;
} wifi_settings_t;

typedef struct __attribute__((packed)) {
    uint16_t in_universe;
    uint16_t out_universe;
    uint16_t repeat_interval;
    uint8_t repeat_time;
} dmx_port_settings_t;

typedef struct __attribute__((packed)) {
    uint16_t universe;
} ws2812_settings_t;

typedef struct __attribute__((packed)) {
    uint16_t universe;
    uint16_t addr;
    uint8_t channel;
    uint8_t groups;
} ambitful_settings_t;

typedef struct __attribute__((packed)) {
    uint16_t magic;           // 0x5844 ('DX')
    uint16_t version;         // Incrementing version
    wifi_settings_t wifi;
    uint32_t enabled_modules;
    dmx_port_settings_t dmx_ports[4];
    ws2812_settings_t ws2812_ports[4];
    ambitful_settings_t ambitful;
    uint8_t reserved[36];     // Padding for future use
} app_config_t;

#define APP_CONFIG_MAGIC   0x5844
#define APP_CONFIG_VERSION 1

/**
 * @brief Initialize configuration with default values.
 * @param[out] config Pointer to the configuration structure to populate.
 */
void app_config_get_default(app_config_t *config);

#define RETURN_ON_ERROR(x) do {        \
    esp_err_t err_rc_ = (x);           \
    if (unlikely(err_rc_ != ESP_OK)) { \
        return err_rc_;                \
    }                                  \
} while(0)

#define RETURN_ON_NULL(ptr, err_code) do { \
    if (unlikely(!(ptr))) { \
        return (err_code); \
    } \
} while(0)

#define LOG_ON_ERROR(x, log_tag, format) do { \
    esp_err_t err_rc_ = (x); \
    if (unlikely(err_rc_ != ESP_OK)) { \
        ESP_LOGE(log_tag, format ": %d", err_rc_); \
    } \
} while(0)

#endif // APP_CONFIG_H
