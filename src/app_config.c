#include "app_config.h"
#include <string.h>
#include <stdio.h> // For sprintf
#include "esp_mac.h" // For esp_read_mac
#include "esp_netif.h" // For esp_ip4addr_aton


void app_config_get_default(app_config_t *config) {
    if (config == NULL) {
        return;
    }

    // Default STA settings
    strncpy(config->sta_ssid, "YOUR_STA_SSID", MAX_SSID_LEN);
    config->sta_ssid[MAX_SSID_LEN] = '\0';
    strncpy(config->sta_password, "YOUR_STA_PASSWORD", MAX_PASSWORD_LEN);
    config->sta_password[MAX_PASSWORD_LEN] = '\0';
    config->sta_dhcp_enabled = true;
    // Default static IP settings (if DHCP is disabled)
    // These values are examples and should be chosen carefully for your network
    config->sta_ip = esp_ip4addr_aton("192.168.1.100");
    config->sta_netmask_len = 24;
    config->sta_gateway = esp_ip4addr_aton("192.168.1.1");

    // Default AP settings
    // Generate AP SSID dynamically based on MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP); // Use AP MAC for AP SSID
    sprintf(config->ap_ssid, "ESP-DMX-%02X%02X", mac[4], mac[5]);
    config->ap_ssid[MAX_SSID_LEN] = '\0';
    strncpy(config->ap_password, "password", MAX_PASSWORD_LEN); // Default AP password
    config->ap_password[MAX_PASSWORD_LEN] = '\0';

    // Default BLE Beacon settings
    config->ble_interval = 20; // 20ms advertising interval
    config->ble_duration_ms = 200; // 25s duration

    config->dmx_in_universe = 0;
    config->dmx_out_universe = 0;

    config->ambitful_universe = 1;
    config->ambitful_addr = 1;
    config->ambitful_channel = 1;
    config->ambitful_groups = 4;
}
