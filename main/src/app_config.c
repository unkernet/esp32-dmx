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
    strncpy(config->wifi.sta.ssid, "YOUR_STA_SSID", MAX_SSID_LEN);
    strncpy(config->wifi.sta.password, "YOUR_STA_PASSWORD", MAX_PASSWORD_LEN);
    config->wifi.sta.dhcp_enabled = true;
    config->wifi.sta.ip = esp_ip4addr_aton("192.168.1.100");
    config->wifi.sta.netmask_len = 24;

    // Default AP settings
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sprintf(config->wifi.ap.ssid, "ESP-DMX-%02X%02X", mac[4], mac[5]);

    config->enabled_modules = DEFAULT_ENABLED_MODULES;

    config->dmx_ports[0].out_universe = 0;
    config->dmx_ports[0].in_universe = 1;
    config->dmx_ports[0].repeat_interval = (550 / 5);
    config->dmx_ports[0].repeat_time = 0xff;

    config->dmx_ports[1].out_universe = 2;
    config->dmx_ports[1].in_universe = 3;
    config->dmx_ports[1].repeat_interval = (550 / 5);
    config->dmx_ports[1].repeat_time = 0xff;

    config->ambitful.universe = 10;
    config->ambitful.addr = 0;
    config->ambitful.channel = 1;
    config->ambitful.groups = 4;

    config->ws2812_ports[0].universe = 20;
}
