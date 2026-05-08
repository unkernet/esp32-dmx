#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

/** @brief GPIO for status LED */
#define LED_GPIO      8

/** @name DMX Port 1 Pins */
/** @{ */
#define DMX_RX_PIN    5
#define DMX_TX_PIN    6
// #define DMX_EN        -1
/** @} */

/** @name DMX Port 2 Pins (Optional) */
/** @{ */
#define DMX_2_RX_PIN    3
#define DMX_2_TX_PIN    4
#define DMX_2_EN        1
/** @} */

/** @brief GPIO for WS2812 pixel data */
#define WS2812_PIN    7

/** @brief Enable Lua interpreter support (~200Kb flash) */
#define LUA_INTERPRETER

#endif // HARDWARE_CONFIG_H
