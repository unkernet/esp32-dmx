#include "app_config.h"
#include <string.h>
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
    strncpy(config->ap_ssid, "ESP32_DMX_AP", MAX_SSID_LEN);
    config->ap_ssid[MAX_SSID_LEN] = '\0';
    strncpy(config->ap_password, "password", MAX_PASSWORD_LEN); // Default AP password
    config->ap_password[MAX_PASSWORD_LEN] = '\0';
    // Default AP IP settings
    config->ap_ip = esp_ip4addr_aton("192.168.4.1");
    config->ap_netmask_len = 24;
    config->ap_gateway = esp_ip4addr_aton("192.168.4.1"); // AP gateway is usually its own IP

    // Default BLE Beacon settings
    config->ble_interval = 30; // 30ms advertising interval
    config->ble_duration_ms = 25000; // 25s duration

    config->dmx_in_universe = 0;
    config->dmx_out_universe = 0;

    config->ambitful_universe = 1;
    config->ambitful_addr = 0;
    config->ambitful_channels = 1;
    config->ambitful_groups = 4;
}
