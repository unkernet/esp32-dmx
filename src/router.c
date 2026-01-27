#include "router.h"
#include "ambitful_ble.h"
#include "artnet_server.h"
#include "dmx.h"
#include "web_server.h"
#include "ws2812.h"

void route_dmx_data(dmx_data_source_t source, uint16_t universe, const uint8_t *data, uint16_t length) {
    send_ambitful_dmx_data(universe, data, length);
    send_ws2812_data(universe, data, length);
    send_dmx_data(universe, data, length);

    if (source != DATA_SOURCE_WS) {
        send_ws_dmx_data(universe, data, length);
    }

    if (source != DATA_SOURCE_ARTNET) {
        send_artnet_dmx_data(universe, data, length);
    }
}
