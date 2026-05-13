#include "dns.h"
#include <mdns.h>
#include <stddef.h>
#include <stdio.h>
#include "esp_mac.h"
#include "app_config.h"
#include "lwip/apps/netbiosns.h"

extern uint32_t g_ip_addr;

esp_err_t start_mdns() {
    uint8_t mac[6];
    char hostname[16];
    mdns_ip_addr_t ip_addr;
    ip_addr.addr.type = ESP_IPADDR_TYPE_V4;
    ip_addr.addr.u_addr.ip4.addr = g_ip_addr;
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        return err;
    }
    snprintf(hostname, sizeof(hostname), "esp-dmx-%02x%02x", mac[4], mac[5]);

    RETURN_ON_ERROR(mdns_init());
    RETURN_ON_ERROR(mdns_hostname_set(hostname));
    RETURN_ON_ERROR(mdns_delegate_hostname_add("esp-dmx", &ip_addr));
    RETURN_ON_ERROR(mdns_service_add("esp-dmx", "_http", "_tcp", 80, NULL, 0));

    netbiosns_set_name("esp-dmx");
    netbiosns_init();

    return ESP_OK;
}
