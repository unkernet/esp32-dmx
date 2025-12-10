#include <string.h>
#include <sys/stat.h> // For stat()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "web_server.h"
#include "app_config.h"
#include "app_config_nvs.h"
#include "globals.h"
#include "artnet_server.h"

#define MAX_WS_CLIENTS 5

static const char *TAG = "WEB_SERVER";

ws_client_info_t ws_clients[MAX_WS_CLIENTS];
int ws_clients_count = 0;

extern void esp_restart(void);
extern app_config_t app_config; // Declare global app_config from main.c

static app_config_t *global_web_config; // Pointer to the global configuration

// Helper to check if a file exists and get its size
static esp_err_t get_file_info(const char *filepath, struct stat *st) {
    if (stat(filepath, st) == -1) {
        // ESP_LOGD(TAG, "Failed to stat file : %s", filepath); // Use D for debug as file might not exist
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Helper to set Content-Type header based on file extension (before .gz)
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath_without_gz) {
    if (strstr(filepath_without_gz, ".html")) {
        httpd_resp_set_type(req, "text/html");
    } else if (strstr(filepath_without_gz, ".js")) {
        httpd_resp_set_type(req, "application/javascript");
    } else if (strstr(filepath_without_gz, ".css")) {
        httpd_resp_set_type(req, "text/css");
    } else if (strstr(filepath_without_gz, ".png")) {
        httpd_resp_set_type(req, "image/png");
    } else if (strstr(filepath_without_gz, ".jpg")) {
        httpd_resp_set_type(req, "image/jpeg");
    } else {
        httpd_resp_set_type(req, "application/octet-stream");
    }
    return ESP_OK;
}

static esp_err_t serve_static_file(httpd_req_t *req)
{
    char base_filepath[128]; // Path without /spiffs and without .gz
    char full_filepath_gz[128]; // Full path including /spiffs and .gz
    const char *uri = req->uri;

    // Determine the base file path (e.g., /index.html or /index.js)
    if (strcmp(uri, "/") == 0) {
        strcpy(base_filepath, "/index.html");
    } else {
        strncpy(base_filepath, uri, sizeof(base_filepath) - 1);
        base_filepath[sizeof(base_filepath) - 1] = '\0';
    }

    // Construct the full gzipped file path in SPIFFS
    snprintf(full_filepath_gz, sizeof(full_filepath_gz), "/spiffs%s.gz", base_filepath);

    struct stat st;
    if (get_file_info(full_filepath_gz, &st) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to find gzipped file: %s", full_filepath_gz);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    FILE* f = fopen(full_filepath_gz, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open gzipped file %s", full_filepath_gz);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char*  buf = (char*)malloc(st.st_size);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for file buffer");
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    fread(buf, 1, st.st_size, f);
    fclose(f);

    set_content_type_from_file(req, base_filepath); // Set content type based on original file extension
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_send(req, buf, st.st_size);
    free(buf);
    return ESP_OK;
}

static esp_err_t http_get_config_handler(httpd_req_t *req)
{
    if (global_web_config == NULL) {
        ESP_LOGE(TAG, "Configuration not initialized for web server.");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Config not available");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_send(req, (const char*)global_web_config, sizeof(app_config_t));
    return ESP_OK;
}

static esp_err_t http_put_config_handler(httpd_req_t *req)
{
    if (global_web_config == NULL) {
        ESP_LOGE(TAG, "Configuration not initialized for web server.");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Config not available");
        return ESP_FAIL;
    }

    if (req->content_len != sizeof(app_config_t)) {
        ESP_LOGE(TAG, "Received config size mismatch. Expected %d, got %d", sizeof(app_config_t), req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid config size");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, (char*)global_web_config, req->content_len);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Request timed out");
        } else {
            ESP_LOGE(TAG, "Failed to receive config data: %d", ret);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        }
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Received new configuration. Saving and restarting...");
    esp_err_t err = app_config_save(global_web_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "Configuration saved. Restarting...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    esp_restart();

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

static const httpd_uri_t get_root_uri = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = serve_static_file,
    .user_ctx = NULL
};

static const httpd_uri_t get_js_uri = {
    .uri      = "/index.js",
    .method   = HTTP_GET,
    .handler  = serve_static_file,
    .user_ctx = NULL
};

static const httpd_uri_t get_config_uri = {
    .uri      = "/config",
    .method   = HTTP_GET,
    .handler  = http_get_config_handler,
    .user_ctx = NULL
};

static const httpd_uri_t put_config_uri = {
    .uri      = "/config",
    .method   = HTTP_PUT,
    .handler  = http_put_config_handler,
    .user_ctx = NULL
};

static const httpd_uri_t ws_uri = {
    .uri        = "/ws",
    .method     = HTTP_GET,
    .handler    = ws_handler,
    .user_ctx   = NULL,
    .is_websocket = true
};

httpd_handle_t start_webserver(app_config_t *config)
{
    global_web_config = config; // Store the config pointer

    httpd_handle_t server = NULL;
    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_cfg.uri_match_fn = httpd_uri_match_wildcard; // Enable wildcard matching if needed

    if (httpd_start(&server, &httpd_cfg) == ESP_OK) {
        httpd_register_uri_handler(server, &get_root_uri);
        httpd_register_uri_handler(server, &get_js_uri); // Register handler for index.js
        httpd_register_uri_handler(server, &get_config_uri);
        httpd_register_uri_handler(server, &put_config_uri);
        httpd_register_uri_handler(server, &ws_uri);
    }
    return server;
}

void send_ws_dmx_data(uint8_t universe, const uint8_t * data, uint16_t length) {
    if (ws_clients_count > 0 && length <= 512) {
        uint8_t buf[513];
        buf[0] = universe;
        memcpy(buf + 1, data, length);

        httpd_ws_frame_t ws_pkt;
        ws_pkt.payload = buf;
        ws_pkt.len = length + 1;
        ws_pkt.type = HTTPD_WS_TYPE_BINARY;

        for (int i = 0; i < ws_clients_count; i++) {
            httpd_ws_send_frame_async(ws_clients[i].handle, ws_clients[i].fd, &ws_pkt);
        }
    }
}
