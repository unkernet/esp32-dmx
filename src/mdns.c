#include "mdns.h"
#include <mdns.h>
#include <stddef.h>
#include <stdio.h>
#include "esp_mac.h"

void start_mdns() {
    uint8_t mac[6];
    char hostname[16];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(hostname, sizeof(hostname), "esp-dmx-%02x%02x", mac[4], mac[5]);
    mdns_init();
    mdns_hostname_set(hostname);
    mdns_service_add("esp-dmx", "_http", "_tcp", 80, NULL, 0);
}
