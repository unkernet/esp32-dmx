#include "app_config.h"
#include <string.h>
#include <stdio.h>
#include "esp_mac.h"
#include "esp_netif.h"


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

    // Default AP settings
    // Generate AP SSID dynamically based on MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sprintf(config->ap_ssid, "ESP-DMX-%02X%02X", mac[4], mac[5]);
    config->ap_ssid[MAX_SSID_LEN] = '\0';
    strncpy(config->ap_password, "password", MAX_PASSWORD_LEN); // Default AP password
    config->ap_password[MAX_PASSWORD_LEN] = '\0';

    config->enabled_modules = MOD_EN_DMX_IN | MOD_EN_DMX_OUT | MOD_EN_AMBITFUL | MOD_EN_WS2812;

    config->dmx_out_universe = 0;
    config->dmx_in_universe = 1;
    config->dmx_repeat_interval = (550 / 5); // 550ms
    config->dmx_repeat_time = 0xff; // Endlessly

    config->dmx_2_out_universe = 2;
    config->dmx_2_in_universe = 3;
    config->dmx_2_repeat_interval = (550 / 5); // 550ms
    config->dmx_2_repeat_time = 0xff; // Endlessly

    config->ambitful_universe = 10;
    config->ambitful_addr = 0;
    config->ambitful_channel = 1;
    config->ambitful_groups = 4;

    config->ws2812_universe = 20;
}

