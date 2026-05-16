#ifndef ROUTER_H
#define ROUTER_H

#include <stdint.h>

/**
 * @brief Sources of DMX data for routing.
 */
typedef enum {
    DATA_SOURCE_ARTNET,    ///< Art-Net network input
    DATA_SOURCE_DMX_1_IN,  ///< Hardware DMX Port 1 input
    DATA_SOURCE_DMX_2_IN,  ///< Hardware DMX Port 2 input
    DATA_SOURCE_WS,        ///< WebSocket input
    DATA_SOURCE_LUA,       ///< Lua script generated data
    DATA_SOURCE_LUA_DEBUG, ///< Lua script generated data with debug flag
} dmx_data_source_t;

/**
 * @brief Routes DMX data from a source to all registered consumers.
 * @param source The origin of the DMX data.
 * @param universe The target DMX universe.
 * @param data Pointer to the DMX buffer.
 * @param length Length of the DMX data in bytes.
 */
void route_dmx_data(dmx_data_source_t source, uint16_t universe, const uint8_t *data, uint16_t length);

#endif // ROUTER_H
