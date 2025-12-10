#include "mdns.h"
#include <mdns.h>
#include <stddef.h>

void start_mdns() {
    mdns_init();
    mdns_hostname_set("esp-dmx");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
}
