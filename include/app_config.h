#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// Define maximum lengths for string fields
#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64

#define MOD_EN_DMX_IN      (1<<0)
#define MOD_EN_DMX_OUT     (1<<1)
#define MOD_EN_ARTNET_OUT  (1<<2)
#define MOD_EN_AMBITFUL    (1<<3)
#define MOD_EN_WS2812      (1<<4)
#define MOD_EN_ESPNOW      (1<<5)

typedef enum {
    WIFI_STATE_STA_CONNECTING,
    WIFI_STATE_STA_CONNECTED,
    WIFI_STATE_STA_CONNECTED_MANUAL,
    WIFI_STATE_AP_RUNNING,
    WIFI_STATE_WAIT_RECONNECT,
} wifi_state_t;

// Configuration structure
typedef struct __attribute__((packed)) {
    // Station (STA) mode settings
    char sta_ssid[MAX_SSID_LEN + 1];
    char sta_password[MAX_PASSWORD_LEN + 1];
    uint8_t sta_dhcp_enabled;
    uint32_t sta_ip;        // IP address in network byte order
    uint8_t sta_netmask_len;   // Netmask CIDR prefix length (e.g., 24 for 255.255.255.0)
    uint32_t sta_gateway;   // Gateway in network byte order

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

    uint8_t reserved_1[9];
} app_config_t;

// Function to get default configuration
void app_config_get_default(app_config_t *config);

#endif // APP_CONFIG_H
