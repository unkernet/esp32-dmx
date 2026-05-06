#ifndef ROUTER_H
#define ROUTER_H

#include <stdint.h>

typedef enum {
    DATA_SOURCE_ARTNET,
    DATA_SOURCE_DMX_1_IN,
    DATA_SOURCE_DMX_2_IN,
    DATA_SOURCE_WS,
    DATA_SOURCE_LUA,
    DATA_SOURCE_LUA_DEBUG,
} dmx_data_source_t;

void route_dmx_data(dmx_data_source_t source, uint16_t universe, const uint8_t *data, uint16_t length);

#endif // ROUTER_H
