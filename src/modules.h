#ifndef ENABLED_MODULES_H
#define ENABLED_MODULES_H
#include "hardware_config.h"

#if (defined(DMX_RX_PIN) && (DMX_RX_PIN) >= 0)
    #define _DMX_RX_EN
#endif
#if (defined(DMX_TX_PIN) && (DMX_2X_PIN) >= 0)
    #define _DMX_TX_EN
#endif
#if (defined(_DMX_RX_EN) || defined(_DMX_TX_EN))
    #define _DMX_EN
#endif

#if (defined(DMX_2_RX_PIN) && (DMX_2_RX_PIN) >= 0)
    #define _DMX_2_RX_EN
#endif
#if (defined(DMX_2_TX_PIN) && (DMX_2_TX_PIN) >= 0)
    #define _DMX_2_TX_EN
#endif
#if (defined(_DMX_2_RX_EN) || defined(_DMX_2_TX_EN))
    #define _DMX_2_EN
#endif

#if (defined(WS2812_PIN) && (WS2812_PIN) < 0)
    #undef WS2812_PIN
#endif
#ifndef WS2812_LEN
    #define WS2812_LEN (DMX_LEN)
#endif

#if (defined(LED_GPIO) && (LED_GPIO) < 0)
    #undef LED_GPIO
#endif

#endif // ENABLED_MODULES_H
