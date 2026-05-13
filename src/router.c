#include "modules.h"
#include "router.h"
#include "web_server.h"
#include "artnet_server.h"
#ifdef AMBITFUL_BLE
#include "ambitful_ble.h"
#endif
#ifdef _DMX_EN
#include "dmx.h"
#endif
#ifdef _DMX_2_EN
#include "dmx_2.h"
#endif
#ifdef WS2812_PIN
#include "ws2812.h"
#endif
#ifdef LUA_INTERPRETER
#include "lua_interpreter.h"
#endif

void route_dmx_data(dmx_data_source_t source, uint16_t universe, const uint8_t *data, uint16_t length) {
    #ifdef AMBITFUL_BLE
    send_ambitful_dmx_data(universe, data, length);
    #endif
    #ifdef WS2812_PIN
    send_ws2812_data(universe, data, length);
    #endif
    #ifdef _DMX_EN
    send_dmx_data(universe, data, length);
    #endif
    #ifdef _DMX_2_EN
    send_dmx_2_data(universe, data, length);
    #endif

    if (source != DATA_SOURCE_ARTNET && source != DATA_SOURCE_WS && source != DATA_SOURCE_LUA) {
        send_ws_dmx_data(universe, data, length);
        send_artnet_dmx_data(universe, data, length);
    }

    #ifdef LUA_INTERPRETER
    if (source != DATA_SOURCE_LUA && source != DATA_SOURCE_LUA_DEBUG) {
        send_lua_data(universe, data, length);
    }
    #endif
}
