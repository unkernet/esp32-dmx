#include "dns.h"
#include <mdns.h>
#include <stddef.h>
#include <stdio.h>
#include "esp_mac.h"

extern uint32_t g_ip_addr;

void start_mdns() {
    uint8_t mac[6];
    char hostname[16];
    mdns_ip_addr_t ip_addr;
    ip_addr.addr.type = ESP_IPADDR_TYPE_V4;
    ip_addr.addr.u_addr.ip4.addr = g_ip_addr;
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(hostname, sizeof(hostname), "esp-dmx-%02x%02x", mac[4], mac[5]);
    mdns_init();
    mdns_hostname_set(hostname);
    mdns_delegate_hostname_add("esp-dmx", &ip_addr);
    mdns_service_add("esp-dmx", "_http", "_tcp", 80, NULL, 0);
}
