#include "hardware_config.h"
#include "router.h"
#ifdef AMBITFUL_BLE
#include "ambitful_ble.h"
#endif
#include "artnet_server.h"
#include "dmx.h"
#if defined(DMX_2_RX_PIN) && defined(DMX_2_TX_PIN)
#include "dmx_2.h"
#endif
#include "web_server.h"
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
    #if defined(DMX_RX_PIN) && defined(DMX_TX_PIN)
    send_dmx_data(universe, data, length);
    #endif
    #if defined(DMX_2_RX_PIN) && defined(DMX_2_TX_PIN)
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
