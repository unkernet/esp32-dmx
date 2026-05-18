#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "artnet_server.h"
#include "app_config.h"
#include "esp_netif.h"
#include "router.h"
#include "modules.h"

// Art-Net Constants
#define ARTNET_PORT 6454
#define ARTNET_ID "Art-Net\0"
#define ARTNET_ID_LENGTH 8

// Configuration for this Art-Net Node
#define ARTNET_NODE_SHORT_NAME "ESP-DMX-%02X%02X"
#define ARTNET_NODE_LONG_NAME "ESP-DMX-%02X%02X Art-Net Node"
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
    uint16_t universe;
    uint16_t length; // BE
    uint8_t data[DMX_LEN];
} artdmx_packet_t;

typedef struct {
    uint8_t buffer[UDP_BUFFER_SIZE];
    int len;
    struct sockaddr_in addr;
} udp_packet_t;

static SemaphoreHandle_t tx_sem;
static udp_packet_t rx_packet;
static udp_packet_t tx_packet;

static uint8_t sequence = 1;
static TaskHandle_t srv_task = NULL, send_task = NULL;

static int sock;

static const char *TAG = "ARTNET_SERVER";

static app_config_t *app_config;

extern uint32_t g_ip_addr;
extern uint32_t g_broadcast_addr;
extern uint8_t g_mac_addr[6];
extern wifi_state_t wifi_state;

static void send_artpollreply(struct sockaddr_in *source_addr) {
    if (xSemaphoreTake(tx_sem, 0) != pdTRUE) {
        return;
    }
    uint8_t enabled_modules = app_config->enabled_modules;
    artpollreply_packet_t *reply = (artpollreply_packet_t *)tx_packet.buffer;
    memset(reply, 0, sizeof(*reply));
    
    tx_packet.len = sizeof(*reply);
    tx_packet.addr = *source_addr;

    uint8_t num_ports = ((_BIT_DMX_0_RX_EN | _BIT_DMX_0_TX_EN) ? 1 : 0) + ((_BIT_DMX_1_RX_EN | _BIT_DMX_1_TX_EN) ? 1 : 0) + ((_BIT_DMX_2_RX_EN | _BIT_DMX_2_TX_EN) ? 1 : 0) + ((_BIT_DMX_3_RX_EN | _BIT_DMX_3_TX_EN) ? 1 : 0);

    memcpy(reply->id, ARTNET_ID, ARTNET_ID_LENGTH);
    reply->opcode = ARTNET_OP_POLLREPLY;
    reply->port = ARTNET_PORT; // Port is 6454
    reply->vers_info = htons(1); // Version 1.0
    // reply->net_sw = 0; // Net 0
    // reply->sub_sw = 0; // Sub-Net 0
    reply->oem = htons(0xFF); // Generic OEM
    // reply->ubea_version = 0;
    reply->status1 = 0x20; // Indicator state: Normal, Port-Address programming enabled
    // reply->esta_mfg = (0); // Unregistered manufacturer
    reply->num_ports = htons(num_ports);

    reply->port_types[0] = ((_BIT_DMX_0_RX_EN & enabled_modules & MOD_EN_DMX_0_IN) ? 0x40 : 0) | ((_BIT_DMX_0_TX_EN & enabled_modules & MOD_EN_DMX_0_OUT) ? 0x80 : 0);
    reply->good_input[0] = ((_BIT_DMX_0_RX_EN & enabled_modules & MOD_EN_DMX_0_IN) ? 0x80 : 0); // Data received, no errors
    reply->good_output[0] = ((_BIT_DMX_0_TX_EN & enabled_modules & MOD_EN_DMX_0_OUT) ? 0x80 : 0); // Data transmitted, no errors
    reply->sw_in[0] = app_config->dmx_ports[0].in_universe;
    reply->sw_out[0] = app_config->dmx_ports[0].out_universe;

    reply->port_types[1] = ((_BIT_DMX_1_RX_EN & enabled_modules & MOD_EN_DMX_1_IN) ? 0x40 : 0) | ((_BIT_DMX_1_TX_EN & enabled_modules & MOD_EN_DMX_1_OUT) ? 0x80 : 0);
    reply->good_input[1] = ((_BIT_DMX_1_RX_EN & enabled_modules & MOD_EN_DMX_1_IN) ? 0x80 : 0); 
    reply->good_output[1] = ((_BIT_DMX_1_TX_EN & enabled_modules & MOD_EN_DMX_1_OUT) ? 0x80 : 0);
    reply->sw_in[1] = app_config->dmx_ports[1].in_universe;
    reply->sw_out[1] = app_config->dmx_ports[1].out_universe;

    reply->port_types[2] = ((_BIT_DMX_2_RX_EN & enabled_modules & MOD_EN_DMX_2_IN) ? 0x40 : 0) | ((_BIT_DMX_2_TX_EN & enabled_modules & MOD_EN_DMX_2_OUT) ? 0x80 : 0);
    reply->good_input[2] = ((_BIT_DMX_2_RX_EN & enabled_modules & MOD_EN_DMX_2_IN) ? 0x80 : 0);
    reply->good_output[2] = ((_BIT_DMX_2_TX_EN & enabled_modules & MOD_EN_DMX_2_OUT) ? 0x80 : 0);
    reply->sw_in[2] = app_config->dmx_ports[2].in_universe;
    reply->sw_out[2] = app_config->dmx_ports[2].out_universe;

    reply->port_types[3] = ((_BIT_DMX_3_RX_EN & enabled_modules & MOD_EN_DMX_3_IN) ? 0x40 : 0) | ((_BIT_DMX_3_TX_EN & enabled_modules & MOD_EN_DMX_3_OUT) ? 0x80 : 0);
    reply->good_input[3] = ((_BIT_DMX_3_RX_EN & enabled_modules & MOD_EN_DMX_3_IN) ? 0x80 : 0);
    reply->good_output[3] = ((_BIT_DMX_3_TX_EN & enabled_modules & MOD_EN_DMX_3_OUT) ? 0x80 : 0);
    reply->sw_in[3] = app_config->dmx_ports[3].in_universe;
    reply->sw_out[3] = app_config->dmx_ports[3].out_universe;

    // reply->acn_priority = 0;
    // reply->sw_macro = 0;
    // reply->sw_remote = 0;
    // reply->style = 0; // Stype: Node
    reply->bind_index = 1;
    reply->status2 = 0x01 | (wifi_state == WIFI_STATE_STA_CONNECTED ? 0x06 : (wifi_state == WIFI_STATE_AP_RUNNING ? 0x04 : 0 )); // Supports web browser configuration, DHCP Configured
    reply->refresh_rate = htons(44);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(reply->short_name, sizeof(reply->short_name) - 1, ARTNET_NODE_SHORT_NAME, mac[4], mac[5]);
    snprintf(reply->long_name, sizeof(reply->long_name) - 1, ARTNET_NODE_LONG_NAME, mac[4], mac[5]);
    strncpy(reply->node_report, ARTNET_NODE_REPORT, sizeof(reply->node_report) - 1);

    reply->ip_address = g_ip_addr;
    reply->bind_ip = g_ip_addr;
    memcpy(reply->mac, g_mac_addr, sizeof(reply->mac));

    xTaskNotifyGive(send_task);
}

static void handle_artdmx(const artdmx_packet_t *dmx_packet, int len) {
    if (app_config == NULL || !(app_config->enabled_modules & MOD_EN_ARTNET_IN)) {
        return;
    }
    uint16_t universe = dmx_packet->universe;
    uint16_t length = ntohs(dmx_packet->length);

    if (length > DMX_LEN || len < sizeof(artdmx_packet_t) - (DMX_LEN - length)) {
        ESP_LOGW(TAG, "Received malformed ArtDMX packet (len: %d)", len);
        // Received malformed ArtDMX packet
        return;
    }

    route_dmx_data(DATA_SOURCE_ARTNET, universe, dmx_packet->data, length);
}

static void handle_artnet_packet(const char *rx_buffer, int len, struct sockaddr_in *source_addr) {
    if (app_config == NULL || len < sizeof(artnet_header_t)) {
        return;
    }

    artnet_header_t *header = (artnet_header_t *)rx_buffer;

    if (memcmp(header->id, ARTNET_ID, ARTNET_ID_LENGTH) != 0 || ntohs(header->prot_ver) < 14) {
        return; // Not an Art-Net packet
    }

    switch (header->opcode) {
        case ARTNET_OP_POLL:
            send_artpollreply(source_addr);
            break;
        case ARTNET_OP_DMX:
            handle_artdmx((artdmx_packet_t *)rx_buffer, len);
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
        sendto(sock, tx_packet.buffer, tx_packet.len, 0, (struct sockaddr *)&tx_packet.addr, sizeof(struct sockaddr_in));
        xSemaphoreGive(tx_sem);
    }
    vTaskDelete(NULL);
}

static void artnet_server_task(void *pvParameters)
{
    while (1) {
        socklen_t socklen = sizeof(rx_packet.addr);
        rx_packet.len = recvfrom(sock, rx_packet.buffer, UDP_BUFFER_SIZE, 0, (struct sockaddr *)&rx_packet.addr, &socklen);

        if (rx_packet.len > 0) {
            handle_artnet_packet((const char*)rx_packet.buffer, rx_packet.len, &rx_packet.addr);
        }
    }
    vTaskDelete(NULL);
}

esp_err_t start_artnet_server(app_config_t *config) {
    if ((config->enabled_modules & (MOD_EN_ARTNET_OUT | MOD_EN_ARTNET_IN)) == 0) {
        return ESP_OK;
    }

    RETURN_ON_NULL(tx_sem = xSemaphoreCreateBinary(), ESP_ERR_NO_MEM);
    xSemaphoreGive(tx_sem);

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
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
    }

    xTaskCreate(artnet_server_task, "artnet_server", 2048, NULL, 5, &srv_task);
    RETURN_ON_NULL(srv_task, ESP_ERR_NO_MEM);
    xTaskCreate(artnet_sender_task, "artnet_sender", 1024, NULL, 7, &send_task);
    RETURN_ON_NULL(send_task, ESP_ERR_NO_MEM);

    app_config = config;
    return ESP_OK;
}

void send_artnet_dmx_data(uint16_t universe, const uint8_t * data, uint16_t length, dmx_data_source_t source) {
    if (app_config == NULL || !(app_config->enabled_modules & MOD_EN_ARTNET_OUT) ||
        source == DATA_SOURCE_ARTNET || source == DATA_SOURCE_LUA ||
        (source == DATA_SOURCE_WS && !(app_config->enabled_modules & MOD_EN_ARTNET_WS)))
    {
        // Do not send data back from Art-Net itself,
        // from DATA_SOURCE_LUA (only from DATA_SOURCE_LUA_DEBUG),
        // and from Websocket, if MOD_EN_ARTNET_WS is not enabled
        return;
    }

    if (xSemaphoreTake(tx_sem, 0) != pdTRUE) {
        return;
    }

    if (++sequence == 0) sequence = 1;
    if (length > DMX_LEN) length = DMX_LEN;

    artdmx_packet_t *pkt = (artdmx_packet_t *)tx_packet.buffer;

    memcpy(pkt->header.id, ARTNET_ID, ARTNET_ID_LENGTH);
    pkt->header.opcode = ARTNET_OP_DMX;
    pkt->header.prot_ver = htons(14);
    pkt->sequence = sequence;
    pkt->physical = 0;
    pkt->universe = universe;
    pkt->length = htons(length);
    memcpy(pkt->data, data, length);

    tx_packet.len = sizeof(artdmx_packet_t) - (512 - length);
    tx_packet.addr.sin_family = AF_INET;
    tx_packet.addr.sin_port = htons(ARTNET_PORT);
    tx_packet.addr.sin_addr.s_addr = g_broadcast_addr;

    xTaskNotifyGive(send_task);
}
