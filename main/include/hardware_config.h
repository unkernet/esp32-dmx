#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

/** 
 * @brief Maximum length of a DMX frame.
 * While DMX512 standardizes on 512 slots, this system can be configured to process 
 * non-standard extended frames. It is not recommended to set this below 512.
 */
#define DMX_LEN 512


/** @brief GPIO pin for the Wifi status LED */
#define LED_GPIO      8


/** @name DMX Port 0 Configuration (Optional)
 * Up to 4 DMX channels can be defined, depending on the available UARTs of the ESP32 chip.
 */
/** @{ */
#define DMX_0_NAME      "DMX"
#define DMX_0_UART      1  // UART hardware port number
#define DMX_0_RX_PIN    5  // GPIO for DMX Receive
#define DMX_0_TX_PIN    6  // GPIO for DMX Transmit
#define DMX_0_EN_PIN    -1 // GPIO for transceiver power control, -1 if unused
/** @} */

/** @brief Determines if the UART Break is transmitted after the last slot or before the first slot. */
#define DMX_BREAK_AFTER_SLOT    false


/**
 * @brief GPIO pin for WS2812 LED strip data (~14Kb Flash, Optional)
 * Up to 4 WS2812 channels can be defined, depending on the available RMT channels of the ESP32 chip.
 */
#define WS2812_0_PIN    7

/** @brief Buffer length for WS2812 data. 
 * Can be larger than DMX_LEN to allow for mapping or direct control from Lua/Websockets. 
 */
#define WS2812_LEN    (DMX_LEN * 2)


/** @brief Enable Lua interpreter (~205Kb Flash, Optional) */
#define LUA_INTERPRETER


/** @brief Enable support for Ambitful BLE lighting control (~210Kb Flash, Optional) */
#define AMBITFUL_BLE


/** @brief Enable Art-Net server (Optional) */
#define ARTNET


#endif // HARDWARE_CONFIG_H
