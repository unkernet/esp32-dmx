#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "esp_http_server.h"
#include "udp_server.h"
#include "globals.h"
#include "ble_beacon.h"

static const char *TAG = "UDP_SERVER";

ws_client_info_t ws_clients[MAX_WS_CLIENTS];
int ws_clients_count = 0;

void udp_server_task(void *pvParameters)
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    while (1) {
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(6454);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", 6454);

        while (1) {
            struct sockaddr_in6 source_addr;
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            if (len < 0) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            } else {
                rx_buffer[len] = 0;
                ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

                ble_beacon_set_data((uint8_t*)rx_buffer, len);

                if (ws_clients_count > 0) {
                    httpd_ws_frame_t ws_pkt;
                    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                    ws_pkt.payload = (uint8_t*)rx_buffer;
                    ws_pkt.len = len;
                    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

                    for (int i = 0; i < ws_clients_count; i++) {
                        httpd_ws_send_frame_async(ws_clients[i].handle, ws_clients[i].fd, &ws_pkt);
                    }
                }
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}
