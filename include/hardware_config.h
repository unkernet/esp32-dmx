#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

/** @brief GPIO for status LED */
#define LED_GPIO      8

/** @name DMX Port 1 Pins (~26Kb flash, Optional) */
/** @{ */
#define DMX_NAME      "DMX"
#define DMX_RX_PIN    5
#define DMX_TX_PIN    6
// #define DMX_EN        -1
/** @} */

/** @name DMX Port 2 Pins (~26Kb flash, Optional) */
/** @{ */
#define DMX_2_NAME      "Wireless DMX"
#define DMX_2_RX_PIN    3
#define DMX_2_TX_PIN    4
#define DMX_2_EN        1
/** @} */

/** @brief GPIO for WS2812 pixel data (~14Kb flash, Optional) */
#define WS2812_PIN    7

/** @brief Enable Lua interpreter support (~205Kb flash, Optional) */
#define LUA_INTERPRETER

/** @brief Enable Ambitful BLE (~210Kb flash, Optional) */
#define AMBITFUL_BLE

#endif // HARDWARE_CONFIG_H
