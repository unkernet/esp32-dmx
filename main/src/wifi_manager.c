#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include <lwip/inet.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "wifi_manager.h"
#include "modules.h"

#define WIFI_CONNECT_ATTEMPTS     8
#define WIFI_CONNECT_TIMEOUT_MS  (20 * 1000)
#define WIFI_AP_TIMEOUT_MS       (1 * 60 * 1000)
#define WIFI_RECONNECT_MS        (60 * 1000)

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define EVT_RECONNECT_NOW  BIT2

static const char *TAG = "wifi_mgr";

/* ---------- state ---------- */

wifi_state_t wifi_state;
uint32_t g_ip_addr = 0;
uint32_t g_broadcast_addr = 0;
uint8_t g_mac_addr[6];
static int s_retry_num = 0;

/* ---------- globals ---------- */

static EventGroupHandle_t wifi_event_group;
static TimerHandle_t ap_timer;
static esp_netif_t *sta_netif;
static esp_netif_t *ap_netif;
static app_config_t *cfg;
static TimerHandle_t led_timer;
static bool led_level = false;

/* ---------- LED ---------- */

static void led_hw_set(bool on)
{
    /* active low */
    #ifdef LED_GPIO
    gpio_set_level(LED_GPIO, on ? 0 : 1);
    #endif
}

static void led_timer_cb(TimerHandle_t t)
{
    led_level = !led_level;
    led_hw_set(led_level);
}

static void led_blink_start(void)
{
    if (!led_timer) {
        led_timer = xTimerCreate(
            "led_blink",
            pdMS_TO_TICKS(500),
            pdTRUE,
            NULL,
            led_timer_cb
        );
    }
    led_level = false;
    xTimerStart(led_timer, 0);
}

static void led_blink_stop(void)
{
    if (led_timer) {
        xTimerStop(led_timer, 0);
    }
}

static void led_on(void)
{
    led_blink_stop();
    led_hw_set(true);
}

static void led_off(void)
{
    led_blink_stop();
    led_hw_set(false);
}

static void led_init(void)
{
    #ifdef LED_GPIO
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    led_off();
    #endif
}

/* ---------- forward ---------- */

static bool wifi_start_sta(void);
static void wifi_start_ap(void);

static void set_netif_hostname(esp_netif_t *netif)
{
    if (netif == NULL) return;
    char hostname[16];
    sprintf(hostname, "ESP-DMX-%02X%02X", g_mac_addr[4], g_mac_addr[5]);
    esp_netif_set_hostname(netif, hostname);
}

static void calc_ip_and_broadcast(esp_netif_ip_info_t *ip_info)
{
    uint32_t netmask = ip_info->netmask.addr;
    g_ip_addr = ip_info->ip.addr;
    g_broadcast_addr = (g_ip_addr & netmask) | ~netmask;
}

/* ---------- timers ---------- */

static void ap_timeout_cb(TimerHandle_t t)
{
    ESP_LOGI(TAG, "AP timeout");
    led_off();
    BaseType_t hp = pdFALSE;
    wifi_state = WIFI_STATE_WAIT_RECONNECT;
    xEventGroupSetBitsFromISR(wifi_event_group, EVT_RECONNECT_NOW, &hp);
    portYIELD_FROM_ISR(hp);
}

/* ---------- event handler ---------- */

static void wifi_event_handler(void *arg,
                               esp_event_base_t base,
                               int32_t id,
                               void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && wifi_state != WIFI_STATE_AP_RUNNING) {

        led_off();

        ESP_LOGI(TAG, "STA_DISCONNECTED %d", s_retry_num);

        if (s_retry_num == -1) {
            esp_wifi_start();
            esp_wifi_connect();
            return;
        }

        if (s_retry_num < WIFI_CONNECT_ATTEMPTS) {
            s_retry_num++;
            esp_wifi_start();
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        led_on();

        ip_event_got_ip_t* event = (ip_event_got_ip_t*) data;
        calc_ip_and_broadcast(&event->ip_info);

        s_retry_num = -1;
        wifi_state = cfg->sta_dhcp_enabled ? WIFI_STATE_STA_CONNECTED : WIFI_STATE_STA_CONNECTED_MANUAL;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        if (ap_timer) {
            xTimerStop(ap_timer, 0);
        }
    }
}

/* ---------- STA ---------- */

static bool wifi_start_sta(void)
{
    ESP_LOGI(TAG, "STA start");
    led_off();

    if (!cfg->sta_dhcp_enabled) {
        esp_netif_dhcpc_stop(sta_netif);

        esp_netif_ip_info_t ip_info;
        ip_info.ip.addr = cfg->sta_ip;
        ip_info.gw.addr = 0;
        if (cfg->sta_netmask_len <= 32) {
            ip_info.netmask.addr = htonl(~((1U << (32 - cfg->sta_netmask_len)) - 1));
        } else {
            ip_info.netmask.addr = htonl(0xFFFFFF00);
        }
        
        esp_netif_set_ip_info(sta_netif, &ip_info);
    } else {
        esp_netif_dhcpc_start(sta_netif);
    }

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, cfg->sta_ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, cfg->sta_password, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);

    if (s_retry_num > 0) {
        s_retry_num = 0;
    }
    wifi_state = WIFI_STATE_STA_CONNECTING;

    esp_wifi_start();
    esp_wifi_connect();
    
    esp_read_mac(g_mac_addr, ESP_MAC_WIFI_STA);
    set_netif_hostname(sta_netif);

    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "STA connected");
        return true;
    }

    ESP_LOGW(TAG, "STA failed");
    wifi_state = WIFI_STATE_WAIT_RECONNECT;

    return false;
}

/* ---------- AP ---------- */

static void wifi_start_ap(void)
{
    ESP_LOGI(TAG, "AP start");
    led_blink_start();

    wifi_config_t wc = {0};
    strncpy((char *)wc.ap.ssid, cfg->ap_ssid, sizeof(wc.ap.ssid) - 1);
    strncpy((char *)wc.ap.password, cfg->ap_password, sizeof(wc.ap.password) - 1);
    wc.ap.max_connection = 4;
    wc.ap.authmode = strlen(cfg->ap_password) ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &wc);
    esp_wifi_start();

    esp_read_mac(g_mac_addr, ESP_MAC_WIFI_SOFTAP);
    set_netif_hostname(ap_netif);
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(ap_netif, &ip_info);
    calc_ip_and_broadcast(&ip_info);

    wifi_state = WIFI_STATE_AP_RUNNING;

    if (!ap_timer) {
        ap_timer = xTimerCreate(
            "ap_timer",
            pdMS_TO_TICKS(WIFI_AP_TIMEOUT_MS),
            pdFALSE,
            NULL,
            ap_timeout_cb
        );
    }
    xTimerStart(ap_timer, 0);
}

/* ---------- reconnect task ---------- */

static void reconnect_task(void *arg)
{
    while (1) {
        xEventGroupWaitBits(
            wifi_event_group,
            EVT_RECONNECT_NOW,
            pdTRUE,        // clear on exit
            pdFALSE,
            pdMS_TO_TICKS(WIFI_RECONNECT_MS)
        );
        if (wifi_state == WIFI_STATE_WAIT_RECONNECT) {
            esp_wifi_stop();
            wifi_start_sta();
        }
    }
}

/* ---------- init ---------- */

esp_err_t wifi_manager_scan_wifi(httpd_req_t *req) {
    uint16_t number = 20;
    wifi_ap_record_t *ap_info = malloc(sizeof(wifi_ap_record_t) * number);
    if (ap_info == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    uint16_t ap_count = 0;

    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = true,
    };

    ESP_LOGI(TAG, "Starting WiFi scan...");
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(err));
        free(ap_info);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_FAIL;
    }

    esp_wifi_scan_get_ap_num(&ap_count);
    esp_wifi_scan_get_ap_records(&number, ap_info);
    ESP_LOGI(TAG, "Found %d networks", ap_count);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "[", 1);
    for (int i = 0; i < ap_count; i++) {
        char buf[128];
        uint8_t *bssid = ap_info[i].bssid;
        int len = snprintf(buf, sizeof(buf), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%d,\"bssid\":\"%02x:%02x:%02x:%02x:%02x:%02x\"}",
                           i == 0 ? "" : ",", (char *)ap_info[i].ssid, ap_info[i].rssi, ap_info[i].authmode,
                           bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        httpd_resp_send_chunk(req, buf, len);
    }
    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);

    free(ap_info);
    return ESP_OK;
}

esp_err_t wifi_manager_init(app_config_t *config)
{
    cfg = config;

    led_init();
    wifi_event_group = xEventGroupCreate();

    RETURN_ON_ERROR(esp_netif_init());
    RETURN_ON_ERROR(esp_event_loop_create_default());
    RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    sta_netif = esp_netif_create_default_wifi_sta();
    RETURN_ON_NULL(sta_netif, ESP_ERR_WIFI_NOT_INIT);
    ap_netif  = esp_netif_create_default_wifi_ap();
    RETURN_ON_NULL(ap_netif, ESP_ERR_WIFI_NOT_INIT);

    wifi_init_config_t wicfg = WIFI_INIT_CONFIG_DEFAULT();
    RETURN_ON_ERROR(esp_wifi_init(&wicfg));

    TaskHandle_t reconnect_task_handle;
    xTaskCreate(reconnect_task, "wifi_reconnect", 2048, NULL, 5, &reconnect_task_handle);
    RETURN_ON_NULL(reconnect_task_handle, ESP_ERR_NO_MEM);

    if (strlen(cfg->sta_ssid)) {
        if (!wifi_start_sta()) {
            wifi_start_ap();
        }
    } else {
        wifi_start_ap();
    }

    return ESP_OK;
}
