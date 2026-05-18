#include "app_config.h"
#include <string.h>
#include <stdio.h>
#include "esp_mac.h"
#include "esp_netif.h"
#include "modules.h"

void app_config_get_default(app_config_t *config) {
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->magic = APP_CONFIG_MAGIC;
    config->version = APP_CONFIG_VERSION;

    // Default STA settings
    // config->wifi.sta.ssid is not set
    // config->wifi.sta.password is not set
    config->wifi.sta.dhcp_enabled = true;
    config->wifi.sta.ip = esp_ip4addr_aton("192.168.1.100");
    config->wifi.sta.netmask_len = 24;

    // Default AP settings
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sprintf(config->wifi.ap.ssid, "ESP-DMX-%02X%02X", mac[4], mac[5]);
    // config->wifi.ap.password is not set

    config->enabled_modules = DEFAULT_ENABLED_MODULES;

    for (uint8_t i = 0; i < 4; i++) {
        config->dmx_ports[i].out_universe = (i * 2) + 0; // 0-6
        config->dmx_ports[i].in_universe = (i * 2) + 1; // 1-7
        config->dmx_ports[i].repeat_interval = 50;
        config->dmx_ports[i].repeat_time = 0xff;

        config->ws2812_ports[i].universe = i + 10; // 10-13
    }

    config->ambitful.universe = 15;
    config->ambitful.addr = 1;
    config->ambitful.channel = 1;
    config->ambitful.groups = 4;

}
