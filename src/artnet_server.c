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

static const char *TAG = "ARTNET_SERVER";

ws_client_info_t ws_clients[MAX_WS_CLIENTS];
int ws_clients_count = 0;

// Art-Net Constants
#define ARTNET_PORT 6454
#define ARTNET_ID "Art-Net\0"
#define ARTNET_ID_LENGTH 8

// Art-Net Opcodes
#define ARTNET_OP_POLL 0x2000
#define ARTNET_OP_POLLREPLY 0x2100
#define ARTNET_OP_DMX 0x5000

// Art-Net Packet Structures (simplified for relevant fields)
typedef struct __attribute__((packed)) {
    char id[ARTNET_ID_LENGTH];
    uint16_t opcode;
    uint16_t prot_ver;
} artnet_header_t;

typedef struct __attribute__((packed)) {
    artnet_header_t header;
    uint8_t talk_to_me;
    uint8_t priority;
} artpoll_packet_t;

typedef struct __attribute__((packed)) {
    char id[ARTNET_ID_LENGTH];
    uint16_t opcode;
    uint32_t ip_address;
    uint16_t port;
    uint16_t vers_info;
    uint8_t net_sw;
    uint8_t sub_sw;
    uint16_t oem;
    uint8_t ubea_version;
    uint8_t status1;
    uint16_t esta_mfg;
    char short_name[18];
    char long_name[64];
    char node_report[64];
    uint16_t num_ports;
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
    uint16_t refresh_rate;
    uint8_t filler_2[11];
} artpollreply_packet_t;

typedef struct __attribute__((packed)) {
    artnet_header_t header;
    uint8_t sequence;
    uint8_t physical;
    uint16_t universe;
    uint16_t length;
    uint8_t data[512];
} artdmx_packet_t;

// Configuration for this Art-Net Node
#define ARTNET_NODE_SHORT_NAME "ESP32 DMX"
#define ARTNET_NODE_LONG_NAME "ESP32 DMX Art-Net Node"
#define ARTNET_NODE_REPORT "#0001 0000 OK"
#define ARTNET_LISTEN_UNIVERSE 0 // Hardcoded for now

static void send_artpollreply(int sock, const struct sockaddr_in *source_addr) {
    artpollreply_packet_t reply = {
        .id = ARTNET_ID,
        .opcode = ARTNET_OP_POLLREPLY,
        .port = ARTNET_PORT, // Port is 6454
        .vers_info = htons(1), // Version 1.0
        .net_sw = 0, // Net 0
        .sub_sw = 0, // Sub-Net 0
        .oem = htons(0xFF), // Generic OEM
        .ubea_version = 0,
        .status1 = 0x20, // Indicator state: Normal, Port-Address programming enabled
        .esta_mfg = (0), // Unregistered manufacturer
        .num_ports = htons(1), // One DMX port
        .port_types = {0xC0, 0, 0, 0}, // DMX512, Output, Input
        .good_input = {0x80, 0, 0, 0}, // Data received, no errors
        .good_output = {0x80, 0, 0, 0}, // Data transmitted, no errors
        .sw_in = {ARTNET_LISTEN_UNIVERSE, 0, 0, 0},
        .sw_out = {ARTNET_LISTEN_UNIVERSE, 0, 0, 0}, // Universe we are listening to
        .acn_priority = 0,
        .sw_macro = 0,
        .sw_remote = 0,
        .style = 0, // Stype: Node
        .bind_index = 1,
        .status2 = 0x01, // Supports web browser configuration
        .refresh_rate = htons(44),
    };

    strncpy(reply.short_name, ARTNET_NODE_SHORT_NAME, sizeof(reply.short_name) - 1);
    strncpy(reply.long_name, ARTNET_NODE_LONG_NAME, sizeof(reply.long_name) - 1);
    strncpy(reply.node_report, ARTNET_NODE_REPORT, sizeof(reply.node_report) - 1);

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = NULL;
    esp_mac_type_t mac_type = ESP_MAC_WIFI_SOFTAP; // Default to SoftAP MAC

    // Try STA interface first
    netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        mac_type = ESP_MAC_WIFI_STA;
        ESP_LOGD(TAG, "Using STA IP for ArtPollReply");
    } else {
        // Fallback to AP interface
        netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            mac_type = ESP_MAC_WIFI_SOFTAP;
            ESP_LOGD(TAG, "Using AP IP for ArtPollReply");
        } else {
            ESP_LOGE(TAG, "Failed to get IP info for ArtPollReply from any interface");
            reply.ip_address = 0; // Zero out IP if not found
            // Still try to get SoftAP MAC if no IP found
            esp_read_mac(reply.mac, ESP_MAC_WIFI_SOFTAP);
            goto send_reply_final; // Skip IP-dependent parts
        }
    }

    reply.ip_address = (ip_info.ip.addr);
    
    // MAC address
    esp_read_mac(reply.mac, mac_type);

send_reply_final:
    // Bind IP (same as main IP)
    reply.bind_ip = reply.ip_address;
    reply.bind_index = 0;
    reply.status2 = 0x08; // Supports web browser configuration

    int err = sendto(sock, &reply, sizeof(reply), 0, (struct sockaddr *)source_addr, sizeof(struct sockaddr_in));
    if (err < 0) {
        ESP_LOGE(TAG, "Error sending ArtPollReply: errno %d", errno);
    } else {
        // ESP_LOGD(TAG, "Sent ArtPollReply to %s:%d", inet_ntoa(source_addr->sin_addr), ntohs(source_addr->sin_port));
    }
}

static void handle_artdmx(const artdmx_packet_t *dmx_packet) {
    uint16_t universe = dmx_packet->universe;
    uint16_t length = ntohs(dmx_packet->length);

    // ESP_LOGD(TAG, "Received ArtDMX for Universe %d, Length %d, Sequence %d",
    //             universe, length, dmx_packet->sequence);

    if (universe == ARTNET_LISTEN_UNIVERSE) {

        // Forward DMX data to WebSocket clients
        if (ws_clients_count > 0) {
            // Create a JSON string for DMX data
            // Example: {"universe": 0, "data": [0, 128, 255, ...]}
            char *json_buf = NULL;
            size_t json_buf_len = snprintf(NULL, 0, "{\"universe\":%d,\"data\":[", universe) + (length * 4) + 2 + 1; // Approx size
            json_buf = (char*)malloc(json_buf_len);
            if (json_buf) {
                char *ptr = json_buf;
                ptr += sprintf(ptr, "{\"universe\":%d,\"data\":[", universe);
                for (int i = 0; i < length; i++) {
                    ptr += sprintf(ptr, "%d%s", dmx_packet->data[i], (i == length - 1) ? "" : ",");
                }
                sprintf(ptr, "]}");

                httpd_ws_frame_t ws_pkt;
                memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                ws_pkt.payload = (uint8_t*)json_buf;
                ws_pkt.len = strlen(json_buf);
                ws_pkt.type = HTTPD_WS_TYPE_TEXT;

                for (int i = 0; i < ws_clients_count; i++) {
                    httpd_ws_send_frame_async(ws_clients[i].handle, ws_clients[i].fd, &ws_pkt);
                }
                free(json_buf);
            } else {
                ESP_LOGE(TAG, "Failed to allocate JSON buffer for DMX data");
            }
        }
    }
}

static void handle_artnet_packet(int sock, const char *rx_buffer, int len, const struct sockaddr_in *source_addr) {
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

void artnet_server_task(void *pvParameters)
{
    char rx_buffer[530]; // Max ArtDMX packet size
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    while (1) {
        struct sockaddr_in dest_addr = {
            .sin_addr.s_addr = htonl(INADDR_ANY),
            .sin_family = AF_INET,
            .sin_port = htons(ARTNET_PORT)
        };
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
        ESP_LOGI(TAG, "Socket bound, port %d", ARTNET_PORT);

        while (1) {
            struct sockaddr_in source_addr;
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &socklen);

            if (len < 0) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            } else {
                handle_artnet_packet(sock, rx_buffer, len, &source_addr);
            }
            vTaskDelay((pdMS_TO_TICKS(4)));
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}
