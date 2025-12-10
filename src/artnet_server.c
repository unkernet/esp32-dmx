#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "artnet_server.h"
#include "globals.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "web_server.h"
#include "ambitful_ble.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

// Art-Net Constants
#define ARTNET_PORT 6454
#define ARTNET_ID "Art-Net\0"
#define ARTNET_ID_LENGTH 8

// Configuration for this Art-Net Node
#define ARTNET_NODE_SHORT_NAME "ESP32 DMX"
#define ARTNET_NODE_LONG_NAME "ESP32 DMX Art-Net Node"
#define ARTNET_NODE_REPORT "#0001 0000 OK"

// Art-Net Opcodes
#define ARTNET_OP_POLL 0x2000
#define ARTNET_OP_POLLREPLY 0x2100
#define ARTNET_OP_DMX 0x5000

#define UDP_BUFFER_SIZE (530)

// Art-Net Packet Structures (simplified for relevant fields)
typedef struct __attribute__((packed)) {
    char id[ARTNET_ID_LENGTH];
    uint16_t opcode; // LE
    uint16_t prot_ver; // BE
} artnet_header_t;

typedef struct __attribute__((packed)) {
    artnet_header_t header;
    uint8_t flags;
    uint8_t priority;
} artpoll_packet_t;

typedef struct __attribute__((packed)) {
    char id[ARTNET_ID_LENGTH];
    uint16_t opcode; // LE
    uint32_t ip_address;
    uint16_t port; // LE
    uint16_t vers_info; // BE
    uint8_t net_sw;
    uint8_t sub_sw;
    uint16_t oem; // BE
    uint8_t ubea_version;
    uint8_t status1;
    uint16_t esta_mfg; // LE
    char short_name[18];
    char long_name[64];
    char node_report[64];
    uint16_t num_ports; // BE
    uint8_t port_types[4];
    uint8_t good_input[4];
    uint8_t good_output[4];
    uint8_t sw_in[4];
    uint8_t sw_out[4];
    uint8_t acn_priority;
    uint8_t sw_macro;
    uint8_t sw_remote;
    uint8_t spare[3];
    uint8_t style;
    uint8_t mac[6];
    uint32_t bind_ip;
    uint8_t bind_index;
    uint8_t status2;
    uint8_t filler_1[13];
    uint16_t refresh_rate; // BE
    uint8_t filler_2[11];
} artpollreply_packet_t;

typedef struct __attribute__((packed)) {
    artnet_header_t header;
    uint8_t sequence;
    uint8_t physical;
    uint8_t universe;
    uint8_t sub_universe;
    uint16_t length; // BE
    uint8_t data[512];
} artdmx_packet_t;

typedef struct {
    uint8_t buffer[UDP_BUFFER_SIZE];
    int len;
    struct sockaddr_in source_addr;
} udp_packet_t;

static QueueHandle_t udpQueue;

// Global variables for DMX output
static artdmx_packet_t s_artnet_packet_out = {
    .header = {
        .id = ARTNET_ID,
        .opcode = ARTNET_OP_DMX,
        .prot_ver = htons(14), // Art-Net Protocol Version 14
    },
    .physical = 0,
    .sub_universe = 0, // Assuming sub-universe 0
};
static uint16_t s_artnet_packet_length;
struct sockaddr_in * s_artnet_reply_to;
static SemaphoreHandle_t s_dmx_data_mutex;
static uint8_t sequence = 1;
static TaskHandle_t srv_task = NULL, send_task = NULL;

static int sock;
static struct sockaddr_in broadcast_addr = {
    .sin_family = AF_INET,
    .sin_port = htons(ARTNET_PORT),
    .sin_addr.s_addr = htonl(INADDR_BROADCAST) // Broadcast address
};

static struct sockaddr_in relpy_addr;

static const char *TAG = "ARTNET_SERVER";

static app_config_t *app_config;

static void send_artpollreply(int sock, struct sockaddr_in *source_addr) {
    if (xSemaphoreTake(s_dmx_data_mutex, (TickType_t)0) != pdTRUE) {
        return;
    }
    artpollreply_packet_t * reply = (artpollreply_packet_t *) &s_artnet_packet_out;
    memset(reply + ARTNET_ID_LENGTH, sizeof(artpollreply_packet_t) - ARTNET_ID_LENGTH, 0);
    s_artnet_packet_length = sizeof(artpollreply_packet_t);
    s_artnet_reply_to = source_addr;

    reply->opcode = ARTNET_OP_POLLREPLY;
    reply->port = ARTNET_PORT; // Port is 6454
    reply->vers_info = htons(1); // Version 1.0
    // reply->net_sw = 0; // Net 0
    // reply->sub_sw = 0; // Sub-Net 0
    reply->oem = htons(0xFF); // Generic OEM
    // reply->ubea_version = 0;
    reply->status1 = 0x20; // Indicator state: Normal, Port-Address programming enabled
    // reply->esta_mfg = (0); // Unregistered manufacturer
    reply->num_ports = htons(1); // One DMX port
    reply->port_types[0] = 0xC0; // DMX512, Output, Input
    reply->good_input[0] = 0x80; // Data received, no errors
    reply->good_output[0] = 0x80; // Data transmitted, no errors
    reply->sw_in[0] = app_config->dmx_in_universe;
    reply->sw_out[0] = app_config->dmx_out_universe;
    // reply->acn_priority = 0;
    // reply->sw_macro = 0;
    // reply->sw_remote = 0;
    // reply->style = 0; // Stype: Node
    reply->bind_index = 1;
    reply->status2 = 0x01; // Supports web browser configuration
    reply->refresh_rate = htons(44);

    strncpy(reply->short_name, ARTNET_NODE_SHORT_NAME, sizeof(reply->short_name) - 1);
    strncpy(reply->long_name, ARTNET_NODE_LONG_NAME, sizeof(reply->long_name) - 1);
    strncpy(reply->node_report, ARTNET_NODE_REPORT, sizeof(reply->node_report) - 1);

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = NULL;
    esp_mac_type_t mac_type = ESP_MAC_WIFI_SOFTAP; // Default to SoftAP MAC

    // Try STA interface first
    netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        mac_type = ESP_MAC_WIFI_STA;
    } else {
        // Fallback to AP interface
        netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            mac_type = ESP_MAC_WIFI_SOFTAP;
        } else {
            // ESP_LOGE(TAG, "Failed to get IP info for ArtPollReply from any interface");
            reply->ip_address = 0; // Zero out IP if not found
            // Still try to get SoftAP MAC if no IP found
            esp_read_mac(reply->mac, ESP_MAC_WIFI_SOFTAP);
            goto send_reply_final; // Skip IP-dependent parts
        }
    }

    reply->ip_address = (ip_info.ip.addr);
    
    // MAC address
    esp_read_mac(reply->mac, mac_type);

send_reply_final:
    // Bind IP (same as main IP)
    reply->bind_ip = reply->ip_address;

    xSemaphoreGive(s_dmx_data_mutex);
    xTaskNotifyGive(send_task);
}

static void handle_artdmx(const artdmx_packet_t *dmx_packet) {
    uint8_t universe = dmx_packet->universe;
    uint16_t length = ntohs(dmx_packet->length);

    send_ws_dmx_data(universe, dmx_packet->data, length);
    send_ambitful_dmx_data(universe, dmx_packet->data, length);
    // just for test
    send_artnet_dmx_data(universe, dmx_packet->data, length, dmx_packet->sequence);
}

static void handle_artnet_packet(const char *rx_buffer, int len, struct sockaddr_in *source_addr) {
    if (len < ARTNET_ID_LENGTH + sizeof(uint16_t)) { // Minimum size for ID + OpCode
        return;
    }

    artnet_header_t *header = (artnet_header_t *)rx_buffer;

    if (memcmp(header->id, ARTNET_ID, ARTNET_ID_LENGTH) != 0) {
        return; // Not an Art-Net packet
    }

    switch (header->opcode) {
        case ARTNET_OP_POLL:
            send_artpollreply(sock, source_addr);
            break;
        case ARTNET_OP_DMX:
            if (len >= sizeof(artdmx_packet_t) - (512 - ((artdmx_packet_t*)rx_buffer)->length)) { // Ensure packet is long enough
                handle_artdmx((artdmx_packet_t *)rx_buffer);
            } else {
                ESP_LOGW(TAG, "Received malformed ArtDMX packet (len: %d)", len);
            }
            break;
        default:
            // ESP_LOGD(TAG, "Received unknown Art-Net opcode: 0x%04X", header->opcode);
            break;
    }
}

static void artnet_sender_task(void *pvParameters)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (xSemaphoreTake(s_dmx_data_mutex, (TickType_t)(pdMS_TO_TICKS(1000))) == pdTRUE) { // Try to take mutex, don't block indefinitely
            int tx_err = sendto(sock, &s_artnet_packet_out, s_artnet_packet_length, 0, (struct sockaddr *)s_artnet_reply_to, sizeof(struct sockaddr_in));
            if (tx_err < 0) {
                ESP_LOGE(TAG, "Error sending Art-Net DMX broadcast: errno %d", errno);
            }
            xSemaphoreGive(s_dmx_data_mutex);
        }
    }
    vTaskDelete(NULL);
}

static void artnet_parser_task(void *pvParameters)
{
    udp_packet_t buffer;
    while (1) {
        if (pdPASS == xQueueReceive(udpQueue, &buffer, portMAX_DELAY)) {
            handle_artnet_packet(buffer.buffer, buffer.len, &buffer.source_addr);
        }
    }
    vTaskDelete(NULL);
    
}

static void artnet_server_task(void *pvParameters)
{
    udp_packet_t buffer;

    while (1) {
        socklen_t socklen = sizeof(buffer.source_addr);
        buffer.len = recvfrom(sock, buffer.buffer, UDP_BUFFER_SIZE, 0, (struct sockaddr *)&buffer.source_addr, &socklen);

        if (buffer.len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        } else {
            xQueueSend(udpQueue, &buffer, 0);
        }

    }
    vTaskDelete(NULL);
}

esp_err_t start_artnet_server(app_config_t *config) {
    app_config = config;

    s_dmx_data_mutex = xSemaphoreCreateMutex();
    if (s_dmx_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create DMX data mutex");
        return ESP_FAIL;
    }

    struct sockaddr_in dest_addr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(ARTNET_PORT)
    };
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }
    // Enable broadcast
    int enable_broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &enable_broadcast, sizeof(enable_broadcast));
    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
    }
    ESP_LOGI(TAG, "Socket bound, port %d", ARTNET_PORT);

    udpQueue = xQueueCreate(2, sizeof(udp_packet_t));

    xTaskCreate(artnet_server_task, "artnet_server", 2048, NULL, 7, &srv_task);
    xTaskCreate(artnet_sender_task, "artnet_sender", 1024, NULL, 5, &send_task);
    xTaskCreate(artnet_parser_task, "artnet_parser", 2048, NULL, 5, NULL);
    return ESP_OK;
}

void send_artnet_dmx_data(uint8_t universe, const uint8_t * data, uint16_t length, uint8_t seq) {
    if (length > 512) {
        return;
    }
    if (!seq) {
        if (++sequence == 0) {
            sequence = 1;
        }
        seq = sequence;
    }
    if (xSemaphoreTake(s_dmx_data_mutex, (TickType_t)0) == pdTRUE) {
        s_artnet_packet_out.header.opcode = ARTNET_OP_DMX;
        s_artnet_packet_out.header.prot_ver = htons(14);
        s_artnet_packet_out.sequence = seq;
        s_artnet_packet_out.physical = 0;
        s_artnet_packet_out.universe = universe;
        s_artnet_packet_out.sub_universe = 0;
        s_artnet_packet_out.length = htons(length);
        memcpy(s_artnet_packet_out.data, data, length);
        s_artnet_packet_length = sizeof s_artnet_packet_out - (512 - length);
        s_artnet_reply_to = &broadcast_addr;
        xSemaphoreGive(s_dmx_data_mutex);
        xTaskNotifyGive(send_task);;
    }
}
