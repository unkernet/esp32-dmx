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

/** @name Module Enable Flags */
/** @{ */
#define MOD_EN_DMX_IN      (1<<0)
#define MOD_EN_DMX_OUT     (1<<1)
#define MOD_EN_ARTNET_OUT  (1<<2)
#define MOD_EN_AMBITFUL    (1<<3)
#define MOD_EN_WS2812      (1<<4)
#define MOD_EN_LUA         (1<<5)
#define MOD_EN_DMX_2_IN      (1<<6)
#define MOD_EN_DMX_2_OUT     (1<<7)
/** @} */

/**
 * @brief WiFi connection states
 */
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
    char sta_ssid[MAX_SSID_LEN + 1];
    char sta_password[MAX_PASSWORD_LEN + 1];
    uint8_t sta_dhcp_enabled;
    uint32_t sta_ip;        // IP address in network byte order
    uint8_t sta_netmask_len;   // Netmask CIDR prefix length (e.g., 24 for 255.255.255.0)

    uint8_t dmx_repeat_time;
    uint8_t dmx_2_repeat_time;
    uint8_t reserved_0[2];

    // Access Point (AP) mode settings
    char ap_ssid[MAX_SSID_LEN + 1];
    char ap_password[MAX_PASSWORD_LEN + 1];

    uint8_t enabled_modules;

    // Ambitful settings
    uint16_t ambitful_universe;
    uint16_t ambitful_addr; // First DMX address
    uint8_t ambitful_channel; // Used channel
    uint8_t ambitful_groups; // Amount of used groups

    // DMX settings
    uint16_t dmx_in_universe;
    uint16_t dmx_out_universe;

    // WS2812 settings
    uint16_t ws2812_universe;

    uint8_t dmx_repeat_interval; // * 5ms

    // DMX 2 settings
    uint16_t dmx_2_in_universe;
    uint16_t dmx_2_out_universe;
    uint8_t dmx_2_repeat_interval;

    uint8_t reserved_1[4];
} app_config_t;

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
