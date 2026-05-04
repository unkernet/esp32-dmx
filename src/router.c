#include "hardware_config.h"
#include "router.h"
#include "ambitful_ble.h"
#include "artnet_server.h"
#include "dmx.h"
#if defined(DMX_2_RX_PIN) && defined(DMX_2_TX_PIN)
#include "dmx_2.h"
#endif
#include "web_server.h"
#include "ws2812.h"
#include "lua_interpreter.h"

void route_dmx_data(dmx_data_source_t source, uint16_t universe, const uint8_t *data, uint16_t length) {
    send_ambitful_dmx_data(universe, data, length);
    send_ws2812_data(universe, data, length);
    send_dmx_data(universe, data, length);
    #if defined(DMX_2_RX_PIN) && defined(DMX_2_TX_PIN)
    send_dmx_2_data(universe, data, length);
    #endif

    if (source != DATA_SOURCE_ARTNET && source != DATA_SOURCE_WS) {
        send_ws_dmx_data(universe, data, length);
        send_artnet_dmx_data(universe, data, length);
    }

    if (source != DATA_SOURCE_LUA) {
        send_lua_data(universe, data, length);
    }
}
