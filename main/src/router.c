#include "modules.h"
#include "router.h"
#include "web_server.h"
#ifdef ARTNET
#include "artnet_server.h"
#endif
#ifdef AMBITFUL_BLE
#include "ambitful_ble.h"
#endif
#ifdef _DMX_EN
#include "dmx.h"
#endif
#ifdef _WS2812_EN
#include "ws2812.h"
#endif
#ifdef LUA_INTERPRETER
#include "lua_interpreter.h"
#endif

void route_dmx_data(dmx_data_source_t source, uint16_t universe, const uint8_t *data, uint16_t length) {
    #ifdef AMBITFUL_BLE
    send_ambitful_dmx_data(universe, data, length);
    #endif
    #ifdef _WS2812_EN
    send_ws2812_data(universe, data, length);
    #endif
    #ifdef _DMX_EN
    dmx_send(universe, data, length);
    #endif
    #ifdef ARTNET
    send_artnet_dmx_data(universe, data, length, source);
    #endif
    send_ws_dmx_data(universe, data, length, source);

    #ifdef LUA_INTERPRETER
    if (source != DATA_SOURCE_LUA && source != DATA_SOURCE_LUA_DEBUG) {
        send_lua_data(universe, data, length);
    }
    #endif
}
