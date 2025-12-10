#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/ip_addr.h" // For IP address structures
#include "wifi_manager.h"
#include "app_config.h"
#include "app_config_nvs.h" // For saving/loading config

static const char *TAG = "WIFI_MANAGER";

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;

static int s_retry_num = 0;
static app_config_t *global_app_config; // Pointer to the global configuration

extern void blink_led(int times);

// Helper function to convert CIDR prefix length to uint32_t netmask
static uint32_t cidr_len_to_ip_netmask(uint8_t cidr_len) {
    if (cidr_len > 32) {
        return 0; // Invalid CIDR length
    }
    return (0xFFFFFFFF << (32 - cidr_len));
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(app_config_t *config) // Made static
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    if (!config->sta_dhcp_enabled) {
        ESP_LOGI(TAG, "Configuring static IP for STA mode.");
        esp_netif_dhcpc_stop(sta_netif); // Corrected: use dhcpc_stop for STA client
        esp_netif_ip_info_t ip_info;
        ip_info.ip.addr = config->sta_ip;
        ip_info.netmask.addr = cidr_len_to_ip_netmask(config->sta_netmask_len);
        ip_info.gw.addr = config->sta_gateway;
        ESP_ERROR_CHECK(esp_netif_set_ip_info(sta_netif, &ip_info));
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char*)wifi_config.sta.ssid, config->sta_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, config->sta_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';


    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(5000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", config->sta_ssid);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", config->sta_ssid);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

static void wifi_init_ap(app_config_t *config) // Made static
{
    ESP_ERROR_CHECK(esp_netif_init());
    blink_led(1);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    blink_led(1);
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    blink_led(1);

    // Configure static IP for AP mode
    esp_netif_dhcps_stop(ap_netif); // Corrected: use dhcps_stop for AP server
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = config->ap_ip;
    ip_info.netmask.addr = cidr_len_to_ip_netmask(config->ap_netmask_len);
    ip_info.gw.addr = config->ap_gateway;
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif)); // Start DHCP server for clients

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    blink_led(1);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(config->ap_ssid),
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    strncpy((char*)wifi_config.ap.ssid, config->ap_ssid, sizeof(wifi_config.ap.ssid));
    strncpy((char*)wifi_config.ap.password, config->ap_password, sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid[sizeof(wifi_config.ap.ssid) - 1] = '\0';
    wifi_config.ap.password[sizeof(wifi_config.ap.password) - 1] = '\0';

    if (strlen(config->ap_password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    blink_led(1);
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    blink_led(1);
    ESP_ERROR_CHECK(esp_wifi_start());
    blink_led(1);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    ESP_LOGI(TAG, "wifi_init_ap finished. SSID:%s password:%s",
             config->ap_ssid, config->ap_password);
}

void wifi_manager_init(app_config_t *config) {
    global_app_config = config; // Store config globally if needed by event handlers or other functions

    // Check if STA SSID is configured to decide between STA and AP mode
    if (strlen(config->sta_ssid) > 0 && strcmp(config->sta_ssid, "YOUR_STA_SSID") != 0) {
        ESP_LOGI(TAG, "STA SSID configured, attempting to connect in STA mode.");
        wifi_init_sta(config);
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdFALSE,
                pdFALSE,
                portMAX_DELAY); // Wait indefinitely for connection result

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "STA mode connected successfully.");
        } else {
            ESP_LOGW(TAG, "STA mode failed to connect, falling back to AP mode.");
            wifi_init_ap(config);
        }
    } else {
        ESP_LOGI(TAG, "No STA SSID configured, starting in AP mode.");
        wifi_init_ap(config);
    }
}
