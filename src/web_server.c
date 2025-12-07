#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "web_server.h"
#include "wifi_manager.h"
#include "globals.h"

static const char *TAG = "WEB_SERVER";

extern void esp_restart(void);

static esp_err_t http_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;
    
    FILE* f = fopen("/spiffs/index.html", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open index.html");
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    buf_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(buf_len);
    fread(buf, 1, buf_len, f);
    fclose(f);

    httpd_resp_send(req, buf, buf_len);
    free(buf);
    return ESP_OK;
}

static esp_err_t http_get_config_handler(httpd_req_t *req)
{
    char ssid[32];
    char password[64];
    if (read_wifi_config(ssid, sizeof(ssid), password, sizeof(password)) == ESP_OK) {
        char json_buf[200];
        snprintf(json_buf, sizeof(json_buf), "{\"ssid\": \"%s\", \"password\": \"%s\"}", ssid, password);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_buf, strlen(json_buf));
    } else {
        httpd_resp_send_404(req);
    }
    return ESP_OK;
}

static esp_err_t http_post_config_handler(httpd_req_t *req)
{
    char buf[100];
    int ret, remaining = req->content_len;

    if (remaining > sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }

    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    buf[req->content_len] = '\0';

    char ssid[32] = {0};
    char password[64] = {0};

    if (httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid)) == ESP_OK &&
        httpd_query_key_value(buf, "password", password, sizeof(password)) == ESP_OK) {
        
        ESP_LOGI(TAG, "Saving new WiFi config: SSID=%s", ssid);
        save_wifi_config(ssid, password);
        httpd_resp_send(req, "WiFi configuration saved. Restarting...", HTTPD_RESP_USE_STRLEN);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        esp_restart();
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form data");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void remove_ws_client(int fd)
{
    int i;
    for (i = 0; i < ws_clients_count; i++) {
        if (ws_clients[i].fd == fd) {
            ESP_LOGI(TAG, "Client disconnected: %d", fd);
            for (int j = i; j < ws_clients_count - 1; j++) {
                ws_clients[j] = ws_clients[j + 1];
            }
            ws_clients_count--;
            break;
        }
    }
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        if (ws_clients_count < MAX_WS_CLIENTS) {
            ws_clients[ws_clients_count].fd = httpd_req_to_sockfd(req);
            ws_clients[ws_clients_count].handle = req->handle;
            ws_clients_count++;
        }
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_HTTPD_INVALID_REQ) {
            remove_ws_client(httpd_req_to_sockfd(req));
        }
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
        return ret;
    }

    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
        httpd_ws_send_frame(req, &ws_pkt);
        free(buf);
    }

    return ESP_OK;
}

static const httpd_uri_t get_uri = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = http_get_handler,
    .user_ctx = NULL
};

static const httpd_uri_t post_config_uri = {
    .uri      = "/config",
    .method   = HTTP_POST,
    .handler  = http_post_config_handler,
    .user_ctx = NULL
};

static const httpd_uri_t get_config_uri = {
    .uri      = "/config",
    .method   = HTTP_GET,
    .handler  = http_get_config_handler,
    .user_ctx = NULL
};

static const httpd_uri_t ws_uri = {
    .uri        = "/ws",
    .method     = HTTP_GET,
    .handler    = ws_handler,
    .user_ctx   = NULL,
    .is_websocket = true
};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &get_uri);
        httpd_register_uri_handler(server, &post_config_uri);
        httpd_register_uri_handler(server, &get_config_uri);
        httpd_register_uri_handler(server, &ws_uri);
    }
    return server;
}
