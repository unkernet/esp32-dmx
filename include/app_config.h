#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// Define maximum lengths for string fields
#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64

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
    uint32_t ap_ip;         // IP address in network byte order
    uint8_t ap_netmask_len;    // Netmask CIDR prefix length (e.g., 24 for 255.255.255.0)
    uint32_t ap_gateway;    // Gateway in network byte order

    // Ambitful settings
    uint16_t ble_interval;      // Advertising interval in ms
    uint32_t ble_duration_ms;   // Advertising duration in ms (0 for indefinite)
    uint8_t ambitful_universe;
    uint16_t ambitful_addr; // First DMX address
    uint8_t ambitful_channels; // Amount of used ambitful channels
    uint8_t ambitful_groups; // Amount groups per channel

    // DMX settings
    uint8_t dmx_in_universe;
    uint8_t dmx_out_universe;

    // WS2812 settings
    uint8_t ws2812_universe;
} app_config_t;

// Function to get default configuration
void app_config_get_default(app_config_t *config);

#endif // APP_CONFIG_H
