#ifndef ENABLED_MODULES_H
#define ENABLED_MODULES_H

#include "hardware_config.h"
#ifdef CONFIG_ESP_DMX_HW_OVERRIDE_FILE
#include CONFIG_ESP_DMX_HW_OVERRIDE_FILE
#endif

/** @name Module Enable Flags (32-bit Layout) */
/** @{ */
#define MOD_EN_DMX_0_IN      (1<<0)
#define MOD_EN_DMX_0_OUT     (1<<1)
#define MOD_EN_DMX_1_IN      (1<<2)
#define MOD_EN_DMX_1_OUT     (1<<3)
#define MOD_EN_DMX_2_IN      (1<<4)
#define MOD_EN_DMX_2_OUT     (1<<5)
#define MOD_EN_DMX_3_IN      (1<<6)
#define MOD_EN_DMX_3_OUT     (1<<7)

#define MOD_EN_WS2812_0      (1<<8)
#define MOD_EN_WS2812_1      (1<<9)
#define MOD_EN_WS2812_2      (1<<10)
#define MOD_EN_WS2812_3      (1<<11)

#define MOD_EN_ARTNET_OUT    (1<<12)
#define MOD_EN_ARTNET_IN     (1<<13)
#define MOD_EN_ARTNET_WS     (1<<14)
// Bit 15 Reserved for Art-Net

#define MOD_EN_LUA           (1<<16)
#define MOD_EN_AMBITFUL      (1<<17)
/** @} */

#ifndef DMX_LEN
    #define DMX_LEN 512
#endif

#if (defined(DMX_0_RX_PIN) && (DMX_0_RX_PIN) >= 0)
    #define _DMX_0_RX_EN
    #define _BIT_DMX_0_RX_EN    MOD_EN_DMX_0_IN
#else
    #define _BIT_DMX_0_RX_EN    0
#endif
#if (defined(DMX_0_TX_PIN) && (DMX_0_TX_PIN) >= 0)
    #define _DMX_0_TX_EN
    #define _BIT_DMX_0_TX_EN    MOD_EN_DMX_0_OUT
#else
    #define _BIT_DMX_0_TX_EN    0
#endif
#if (defined(_DMX_0_RX_EN) || defined(_DMX_0_TX_EN))
    #define _DMX_0_EN
#endif

#if (defined(DMX_1_RX_PIN) && (DMX_1_RX_PIN) >= 0)
    #define _DMX_1_RX_EN
    #define _BIT_DMX_1_RX_EN    MOD_EN_DMX_1_IN
#else
    #define _BIT_DMX_1_RX_EN    0
#endif
#if (defined(DMX_1_TX_PIN) && (DMX_1_TX_PIN) >= 0)
    #define _DMX_1_TX_EN
    #define _BIT_DMX_1_TX_EN    MOD_EN_DMX_1_OUT
#else
    #define _BIT_DMX_1_TX_EN    0
#endif
#if (defined(_DMX_1_RX_EN) || defined(_DMX_1_TX_EN))
    #define _DMX_1_EN
#endif

#if (defined(DMX_2_RX_PIN) && (DMX_2_RX_PIN) >= 0)
    #define _DMX_2_RX_EN
    #define _BIT_DMX_2_RX_EN    MOD_EN_DMX_2_IN
#else
    #define _BIT_DMX_2_RX_EN    0
#endif
#if (defined(DMX_2_TX_PIN) && (DMX_2_TX_PIN) >= 0)
    #define _DMX_2_TX_EN
    #define _BIT_DMX_2_TX_EN    MOD_EN_DMX_2_OUT
#else
    #define _BIT_DMX_2_TX_EN    0
#endif
#if (defined(_DMX_2_RX_EN) || defined(_DMX_2_TX_EN))
    #define _DMX_2_EN
#endif

#if (defined(DMX_3_RX_PIN) && (DMX_3_RX_PIN) >= 0)
    #define _DMX_3_RX_EN
    #define _BIT_DMX_3_RX_EN    MOD_EN_DMX_3_IN
#else
    #define _BIT_DMX_3_RX_EN    0
#endif
#if (defined(DMX_3_TX_PIN) && (DMX_3_TX_PIN) >= 0)
    #define _DMX_3_TX_EN
    #define _BIT_DMX_3_TX_EN    MOD_EN_DMX_3_OUT
#else
    #define _BIT_DMX_3_TX_EN    0
#endif
#if (defined(_DMX_3_RX_EN) || defined(_DMX_3_TX_EN))
    #define _DMX_3_EN
#endif

#if (defined(_DMX_0_EN) || defined(_DMX_1_EN) || defined(_DMX_2_EN) || defined(_DMX_3_EN))
    #define _DMX_EN
#endif

#ifndef DMX_BREAK_AFTER_SLOT
    #define DMX_BREAK_AFTER_SLOT    0
#endif

#if (defined(WS2812_0_PIN) && (WS2812_0_PIN) >= 0)
    #define _WS2812_0_EN 1
    #define _BIT_WS2812_0_EN    MOD_EN_WS2812_0
#else
    #define _WS2812_0_EN 0
    #define _BIT_WS2812_0_EN    0
    #undef WS2812_0_PIN
#endif
#if (defined(WS2812_1_PIN) && (WS2812_1_PIN) >= 0)
    #define _WS2812_1_EN 1
    #define _BIT_WS2812_1_EN    MOD_EN_WS2812_1
#else
    #define _WS2812_1_EN 0
    #define _BIT_WS2812_1_EN    0
    #undef WS2812_1_PIN
#endif
#if (defined(WS2812_2_PIN) && (WS2812_2_PIN) >= 0)
    #define _WS2812_2_EN 1
    #define _BIT_WS2812_2_EN    MOD_EN_WS2812_2
#else
    #define _WS2812_2_EN 0
    #define _BIT_WS2812_2_EN    0
    #undef WS2812_2_PIN
#endif
#if (defined(WS2812_3_PIN) && (WS2812_3_PIN) >= 0)
    #define _WS2812_3_EN 1
    #define _BIT_WS2812_3_EN    MOD_EN_WS2812_3
#else
    #define _WS2812_3_EN 0
    #define _BIT_WS2812_3_EN    0
    #undef WS2812_3_PIN
#endif
#if (defined(WS2812_0_PIN) || defined(WS2812_1_PIN) || defined(WS2812_2_PIN) || defined(WS2812_3_PIN))
    #define _WS2812_EN
#endif
#define _WS2812_PORTS_COUNT (_WS2812_0_EN + _WS2812_1_EN + _WS2812_2_EN + _WS2812_3_EN)

#ifndef WS2812_LEN
    #define WS2812_LEN (DMX_LEN)
#endif

#ifdef AMBITFUL_BLE
    #define _BIT_AMBITFUL_BLE_EN    MOD_EN_AMBITFUL
#else
    #define _BIT_AMBITFUL_BLE_EN    0
#endif

#ifdef LUA_INTERPRETER
    #define _BIT_LUA_EN    MOD_EN_LUA
#else
    #define _BIT_LUA_EN    0
#endif

#ifdef ARTNET
    #define _BIT_ARTNET_SUPP    (MOD_EN_ARTNET_OUT | MOD_EN_ARTNET_IN | MOD_EN_ARTNET_WS)
    #define _BIT_ARTNET_EN      (MOD_EN_ARTNET_IN)
#else
    #define _BIT_ARTNET_EN    0
#endif

#ifndef LED_GPIO
    #define LED_GPIO    -1
#endif

#define SUPPORTED_MODULES ( \
    _BIT_DMX_0_RX_EN | _BIT_DMX_0_TX_EN | _BIT_DMX_1_RX_EN | _BIT_DMX_1_TX_EN | _BIT_DMX_2_RX_EN | _BIT_DMX_2_TX_EN | _BIT_DMX_3_RX_EN | _BIT_DMX_3_TX_EN | \
    _BIT_WS2812_0_EN | _BIT_WS2812_1_EN | _BIT_WS2812_2_EN | _BIT_WS2812_3_EN | \
    _BIT_AMBITFUL_BLE_EN | \
    _BIT_LUA_EN | \
    _BIT_ARTNET_SUPP | \
0 )

// By default enable all available hardware modules except AMBITFUL_BLE (consume lot of memory),
// ARTNET_WS (creates CPU load), first WS2812 channel and LUA_INTERPRETER (can not be disabled if supported by the firmware)
#define DEFAULT_ENABLED_MODULES ( \
    _BIT_DMX_0_RX_EN | _BIT_DMX_0_TX_EN | _BIT_DMX_1_RX_EN | _BIT_DMX_1_TX_EN | \
    _BIT_WS2812_0_EN | \
    _BIT_ARTNET_EN | \
0 )

#endif // ENABLED_MODULES_H
