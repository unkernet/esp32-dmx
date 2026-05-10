#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "hardware_config.h"
#include "web_server.h"
#include "app_config.h"
#include "app_config_nvs.h"
#include "router.h"
#include "wifi_manager.h"
#ifdef LUA_INTERPRETER
#include "lua_interpreter.h"
#endif

#define MAX_DATA_SIZE 512

typedef struct {
    httpd_handle_t handle;
    int fd;
    bool active;
} ws_client_info_t;

static const char *TAG = "WEB_SERVER";

static ws_client_info_t active_ws_client = { .handle = NULL, .fd = -1, .active = false };
static SemaphoreHandle_t ws_mutex = NULL;

static uint8_t ws_tx_buf[MAX_DATA_SIZE + 2];
static size_t ws_tx_len = 0;
static TaskHandle_t ws_send_task_handle = NULL;
static SemaphoreHandle_t ws_buffer_sem = NULL;

static void ws_send_task(void *arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        xSemaphoreTake(ws_buffer_sem, portMAX_DELAY);
        xSemaphoreTake(ws_mutex, portMAX_DELAY);
        if (active_ws_client.active) {
            httpd_ws_frame_t ws_pkt = {
                .payload = ws_tx_buf,
                .len = ws_tx_len,
                .type = HTTPD_WS_TYPE_BINARY
            };
            httpd_ws_send_frame_async(active_ws_client.handle, active_ws_client.fd, &ws_pkt);
        }
        xSemaphoreGive(ws_mutex);
        xSemaphoreGive(ws_buffer_sem);
    }
}

extern void esp_restart(void);
static app_config_t *app_config; // Pointer to the global configuration

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

    // Construct the full file path in SPIFFS
    snprintf(full_filepath_gz, sizeof(full_filepath_gz), "/spiffs%s.gz", base_filepath);

    struct stat st;
    if (get_file_info(full_filepath_gz, &st) != ESP_OK) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    FILE* f = fopen(full_filepath_gz, "r");
    if (f == NULL) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    set_content_type_from_file(req, base_filepath); // Set content type based on original file extension
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    char *chunk = malloc(1024);
    if (chunk == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, 1024, f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
            fclose(f);
            free(chunk);
            return ESP_FAIL;
        }
    }
    fclose(f);
    free(chunk);

    httpd_resp_send_chunk(req, NULL, 0); // Send empty chunk to signal end of stream
    return ESP_OK;
}

static esp_err_t http_get_config_handler(httpd_req_t *req)
{
    if (app_config == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Config not available");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_send(req, (const char*)app_config, sizeof(app_config_t));
    return ESP_OK;
}

static esp_err_t http_put_config_handler(httpd_req_t *req)
{
    if (app_config == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Config not available");
        return ESP_FAIL;
    }

    if (req->content_len != sizeof(app_config_t)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid config size");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, (char*)app_config, req->content_len);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Request timed out");
        } else {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        }
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Received new configuration. Saving and restarting...");
    esp_err_t err = app_config_save(app_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        return ESP_FAIL;
    }

    httpd_resp_send(req, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

static esp_err_t http_get_task_list_handler(httpd_req_t *req)
{
    char buf[1024];
    httpd_resp_set_hdr(req, "Content-Type", "text/plain");
    sprintf(buf, "heap: free %d %d block %d int %d min %d\n",
        esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_8BIT),
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        esp_get_free_internal_heap_size(),
        esp_get_minimum_free_heap_size());
    httpd_resp_send_chunk(req, buf, strlen(buf));
    vTaskList(buf);
    httpd_resp_send_chunk(req, buf, strlen(buf));
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}   

static void remove_ws_client(int fd)
{
    xSemaphoreTake(ws_mutex, portMAX_DELAY);
    ESP_LOGI(TAG, "Client disconnected: %d", fd);
    if (active_ws_client.active && active_ws_client.fd == fd) {
        active_ws_client.active = false;
        active_ws_client.fd = -1;
        active_ws_client.handle = NULL;
    }
    close(fd);
    xSemaphoreGive(ws_mutex);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS handshake done");
        xSemaphoreTake(ws_mutex, portMAX_DELAY);
        
        // If there's already a client, disconnect it first
        if (active_ws_client.active) {
            httpd_sess_trigger_close(active_ws_client.handle, active_ws_client.fd);
        }

        active_ws_client.fd = httpd_req_to_sockfd(req);
        active_ws_client.handle = req->handle;
        active_ws_client.active = true;
        
        xSemaphoreGive(ws_mutex);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
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

        if (ws_pkt.type == HTTPD_WS_TYPE_BINARY) {
            if (ws_pkt.len > 2) {
                uint16_t universe = ws_pkt.payload[0] + (ws_pkt.payload[1] << 8);
                const uint8_t *data = (const uint8_t *)(ws_pkt.payload + 2);
                uint16_t len = ws_pkt.len - 2;
                route_dmx_data(DATA_SOURCE_WS, universe, data, len);
            }
        }
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

static esp_err_t http_get_wifi_scan_handler(httpd_req_t *req) {
    return wifi_manager_scan_wifi(req);
}

static const httpd_uri_t get_wifi_scan_uri = {
    .uri      = "/wifi/scan",
    .method   = HTTP_GET,
    .handler  = http_get_wifi_scan_handler,
    .user_ctx = NULL
};

static const httpd_uri_t get_task_list = {
    .uri      = "/task_list",
    .method   = HTTP_GET,
    .handler  = http_get_task_list_handler,
    .user_ctx = NULL
};

#ifdef LUA_INTERPRETER
static esp_err_t http_get_lua_list_handler(httpd_req_t *req) {
    return lua_interpreter_stream_scripts(req);
}

static esp_err_t http_post_lua_run_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Request timed out");
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    esp_err_t err = lua_interpreter_run(buf);
    if (err == ESP_OK) {
        httpd_resp_send(req, NULL, 0);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start script");
    }
    return ESP_OK;
}

static esp_err_t http_post_lua_kill_handler(httpd_req_t *req) {
    if (lua_interpreter_kill() == ESP_OK) {
        httpd_resp_send(req, NULL, 0);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Script terminated");
    }
    return ESP_OK;
}

static esp_err_t http_get_lua_script_handler(httpd_req_t *req) {
    const char *filename = req->uri + strlen("/lua/scripts/");
    if (strlen(filename) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Filename missing");
        return ESP_FAIL;
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/spiffs/%s", filename);

    struct stat st;
    if (stat(filepath, &st) != 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/plain");
    char *buf = malloc(1024);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, 1024, f)) > 0) {
        httpd_resp_send_chunk(req, buf, read_bytes);
    }
    fclose(f);
    free(buf);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t http_put_lua_script_handler(httpd_req_t *req) {
    const char *filename = req->uri + strlen("/lua/scripts/");
    if (strlen(filename) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Filename missing");
        return ESP_FAIL;
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/spiffs/%s", filename);

    if (req->content_len == 0) {
        ESP_LOGI(TAG, "Deleting file: %s", filepath);
        unlink(filepath);
        httpd_resp_sendstr(req, "File deleted");
        return ESP_OK;
    }

    FILE *f = fopen(filepath, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s for writing", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }

    char *buf = malloc(1024);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    int received;
    int remaining = req->content_len;
    while (remaining > 0 && (received = httpd_req_recv(req, buf, remaining > 1024 ? 1024 : remaining)) > 0) {
        fwrite(buf, 1, received, f);
        remaining -= received;
    }
    fclose(f);
    free(buf);

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t get_lua_list_uri = {
    .uri      = "/lua/list",
    .method   = HTTP_GET,
    .handler  = http_get_lua_list_handler,
    .user_ctx = NULL
};

static const httpd_uri_t post_lua_run_uri = {
    .uri      = "/lua/run",
    .method   = HTTP_POST,
    .handler  = http_post_lua_run_handler,
    .user_ctx = NULL
};

static const httpd_uri_t post_lua_kill_uri = {
    .uri      = "/lua/kill",
    .method   = HTTP_POST,
    .handler  = http_post_lua_kill_handler,
    .user_ctx = NULL
};

static const httpd_uri_t get_lua_script_uri = {
    .uri      = "/lua/scripts/*",
    .method   = HTTP_GET,
    .handler  = http_get_lua_script_handler,
    .user_ctx = NULL
};

static const httpd_uri_t put_lua_script_uri = {
    .uri      = "/lua/scripts/*",
    .method   = HTTP_PUT,
    .handler  = http_put_lua_script_handler,
    .user_ctx = NULL
};
#endif

void httpd_close_cb(httpd_handle_t hd, int sockfd)
{
    remove_ws_client(sockfd);
}

esp_err_t start_webserver(app_config_t *config)
{
    RETURN_ON_NULL(ws_mutex = xSemaphoreCreateMutex(), ESP_ERR_NO_MEM);
    RETURN_ON_NULL(ws_buffer_sem = xSemaphoreCreateBinary(), ESP_ERR_NO_MEM);
    xSemaphoreGive(ws_buffer_sem);

    xTaskCreate(ws_send_task, "ws_send_task", 2048, NULL, 5, &ws_send_task_handle);
    RETURN_ON_NULL(ws_send_task_handle, ESP_ERR_NO_MEM);

    httpd_handle_t server = NULL;
    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_cfg.max_uri_handlers = 12; // Increase limit to accommodate Lua API
    httpd_cfg.uri_match_fn = httpd_uri_match_wildcard; // Enable wildcard matching if needed
    httpd_cfg.close_fn = httpd_close_cb;

    RETURN_ON_ERROR(httpd_start(&server, &httpd_cfg));

    httpd_register_uri_handler(server, &get_root_uri);
    httpd_register_uri_handler(server, &get_js_uri);
    httpd_register_uri_handler(server, &get_config_uri);
    httpd_register_uri_handler(server, &put_config_uri);
    httpd_register_uri_handler(server, &ws_uri);
    httpd_register_uri_handler(server, &get_wifi_scan_uri);
    httpd_register_uri_handler(server, &get_task_list);
    #ifdef LUA_INTERPRETER
    httpd_register_uri_handler(server, &get_lua_list_uri);
    httpd_register_uri_handler(server, &post_lua_run_uri);
    httpd_register_uri_handler(server, &post_lua_kill_uri);
    httpd_register_uri_handler(server, &get_lua_script_uri);
    httpd_register_uri_handler(server, &put_lua_script_uri);
    #endif

    app_config = config;
    return ESP_OK;
}

void send_ws_dmx_data(uint16_t universe, const uint8_t * data, uint16_t length) {
    if (!app_config) return;

    if (xSemaphoreTake(ws_mutex, 0) == pdTRUE) {
        bool active = active_ws_client.active;
        xSemaphoreGive(ws_mutex);
        if (!active) return;
    } else {
        return;
    }

    if (xSemaphoreTake(ws_buffer_sem, 0) == pdTRUE) {
        if (length > MAX_DATA_SIZE) length = MAX_DATA_SIZE;
        ws_tx_buf[0] = universe & 0xff;
        ws_tx_buf[1] = universe >> 8;
        memcpy(ws_tx_buf + 2, data, length);
        ws_tx_len = length + 2;

        xSemaphoreGive(ws_buffer_sem);
        xTaskNotifyGive(ws_send_task_handle);
    }
}
