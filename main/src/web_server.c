#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "modules.h"
#include "web_server.h"
#include "app_config.h"
#include "app_config_nvs.h"
#include "router.h"
#include "wifi_manager.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "util.h"
#include "modules.h"
#ifdef LUA_INTERPRETER
#include "lua_interpreter.h"
#endif

#if ( ( configUSE_TRACE_FACILITY == 1 ) && ( configUSE_STATS_FORMATTING_FUNCTIONS > 0 ) )
#define TASK_LIST
#endif

typedef struct {
    httpd_handle_t handle;
    int fd;
    bool active;
} ws_client_info_t;

typedef struct __attribute__((packed)) {
    uint32_t supported;
    char dev_name[5];
    char dmx_name[4][16];
} device_meta_t;

static const char *TAG = "WEB_SERVER";
static ws_client_info_t active_ws_client = { .handle = NULL, .fd = -1, .active = false };
static SemaphoreHandle_t ws_mutex = NULL;
static uint8_t ws_tx_buf[DMX_LEN + 2];
static size_t ws_tx_len = 0;
static TaskHandle_t ws_send_task_handle = NULL;
static SemaphoreHandle_t ws_buffer_mutex = NULL;
static app_config_t *app_config; // Pointer to the global configuration

static void ws_send_task(void *arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        xSemaphoreTake(ws_buffer_mutex, portMAX_DELAY);
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
        xSemaphoreGive(ws_buffer_mutex);
        vTaskDelay(pdMS_TO_TICKS(100)); // Short delay to prevent data sending too often
    }
}

void send_ws_dmx_data(uint16_t universe, const uint8_t * data, uint16_t length, dmx_data_source_t source) {
    if (!app_config) return;

    if (source == DATA_SOURCE_WS || source == DATA_SOURCE_LUA ||
        (source == DATA_SOURCE_ARTNET && !(app_config->enabled_modules & MOD_EN_ARTNET_WS)))
    {
        // Do not send data from Websocket itself,
        // from DATA_SOURCE_LUA (only from DATA_SOURCE_LUA_DEBUG)
        // And from Art-Net, if MOD_EN_ARTNET_WS is not enabled
        return;
    }

    if (xSemaphoreTake(ws_mutex, 0) == pdTRUE) {
        bool active = active_ws_client.active;
        xSemaphoreGive(ws_mutex);
        if (!active) return;
    } else {
        return;
    }

    if (xSemaphoreTake(ws_buffer_mutex, 0) == pdTRUE) {
        if (length > DMX_LEN) length = DMX_LEN;
        ws_tx_buf[0] = universe & 0xff;
        ws_tx_buf[1] = universe >> 8;
        memcpy(ws_tx_buf + 2, data, length);
        ws_tx_len = length + 2;

        xSemaphoreGive(ws_buffer_mutex);
        xTaskNotifyGive(ws_send_task_handle);
    }
}

static esp_err_t http_get_status_handler(httpd_req_t *req) {
    char json_buf[120]; // Sufficient for the JSON response
    int64_t uptime_us = esp_timer_get_time();

    uint32_t heap_total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    uint32_t heap_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    uint32_t heap_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

    snprintf(json_buf, sizeof(json_buf),
        "{"
            "\"uptime\":%lld,"
            "\"heap\":{"
                "\"total\":%lu,"
                "\"free\":%lu,"
                "\"block\":%lu,"
                "\"min\":%lu"
            "}"
        "}",
        uptime_us / 1000000,
        heap_total, heap_free, heap_free_block, heap_min_free
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_buf);
    return ESP_OK;
}

static const httpd_uri_t get_status_uri = {
    .uri      = "/status",
    .method   = HTTP_GET,
    .handler  = http_get_status_handler,
    .user_ctx = NULL
};



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
    char base_filepath[32]; // Path without /data and without .gz
    char full_filepath_gz[48]; // Full path including /data and .gz
    const char *uri = req->uri;

    // Determine the base file path (e.g., /index.html or /index.js)
    if (strcmp(uri, "/") == 0) {
        strcpy(base_filepath, "/index.html");
    } else {
        strncpy(base_filepath, uri, sizeof(base_filepath) - 1);
        base_filepath[sizeof(base_filepath) - 1] = '\0';
    }

    snprintf(full_filepath_gz, sizeof(full_filepath_gz), "/data%s.gz", base_filepath);

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
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate memory");
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
    httpd_resp_send_chunk(req, (const char*)app_config, sizeof(app_config_t));

    device_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    meta.supported = SUPPORTED_MODULES;
    snprintf(meta.dev_name, sizeof(meta.dev_name), "%02X%02X", mac[4], mac[5]);
    #ifdef _DMX_0_EN
    strncpy(meta.dmx_name[0], DMX_0_NAME, sizeof(meta.dmx_name[0]) - 1);
    #endif
    #ifdef _DMX_1_EN
    strncpy(meta.dmx_name[1], DMX_1_NAME, sizeof(meta.dmx_name[1]) - 1);
    #endif
    #ifdef _DMX_2_EN
    strncpy(meta.dmx_name[2], DMX_2_NAME, sizeof(meta.dmx_name[2]) - 1);
    #endif
    #ifdef _DMX_3_EN
    strncpy(meta.dmx_name[3], DMX_3_NAME, sizeof(meta.dmx_name[3]) - 1);
    #endif
    httpd_resp_send_chunk(req, (const char*)&meta, sizeof(meta));

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t http_put_config_handler(httpd_req_t *req)
{
    if (app_config == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Config not available");
        return ESP_FAIL;
    }

    if (req->content_len == 0) {
        // Just reboot
        httpd_resp_send(req, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return ESP_OK;
    }

    if (req->content_len != sizeof(app_config_t)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid config size");
        return ESP_FAIL;
    }

    app_config_t new_config;
    int ret = httpd_req_recv(req, (char*)&new_config, req->content_len);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Request timed out");
        } else {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        }
        return ESP_FAIL;
    }

    if (new_config.magic != app_config->magic || new_config.version != app_config->version) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid config header");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Received new configuration. Saving and restarting...");
    esp_err_t err = app_config_save(&new_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        return ESP_FAIL;
    }

    httpd_resp_send(req, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK;
}

#ifdef TASK_LIST
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

static const httpd_uri_t get_task_list = {
    .uri      = "/task_list",
    .method   = HTTP_GET,
    .handler  = http_get_task_list_handler,
    .user_ctx = NULL
};
#endif

static void remove_ws_client(int fd)
{
    xSemaphoreTake(ws_mutex, portMAX_DELAY);
    if (active_ws_client.active && active_ws_client.fd == fd) {
        ESP_LOGD(TAG, "WS client disconnected");
        active_ws_client.active = false;
        active_ws_client.fd = -1;
        active_ws_client.handle = NULL;
    }
    // NOTE: esp_http_server does NOT actually close WebSocket sockets when the
    // session is torn down, the socket handles leak and the server eventually
    // runs out of slots. Keep the explicit close() here.
    close(fd);
    xSemaphoreGive(ws_mutex);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGD(TAG, "WS handshake done");
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
        buf = malloc(ws_pkt.len + 1);
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

static const httpd_uri_t get_css_uri = {
    .uri      = "/index.css",
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


#ifdef LUA_INTERPRETER
static esp_err_t http_get_lua_list_handler(httpd_req_t *req) {
    return lua_interpreter_list_scripts(req);
}

static esp_err_t http_post_lua_run_handler(httpd_req_t *req) {
    const char *filename = req->uri + strlen("/lua/run/");

    esp_err_t err;
    if (strlen(filename) > 0) {
        // Run script from SPIFFS
        err = lua_interpreter_run(filename);
    } else {
        // Run script from HTTP body stream
        err = lua_interpreter_run_stream(req);
    }

    if (err == ESP_OK) {
        httpd_resp_send(req, NULL, 0);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to run script");
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
    size_t len = strlen(filename);
    if (len == 0 || len > 30) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, NULL);
        return ESP_FAIL;
    }

    char filepath[48];
    snprintf(filepath, sizeof(filepath), "/user/%s", filename);

    struct stat st;
    if (stat(filepath, &st) != 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, NULL);
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
    size_t len = strlen(filename);

    if (len == 0 || len > 30 || (!ends_with(filename, ".lua") && !ends_with(filename, ".luac"))) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, NULL);
        return ESP_FAIL;
    }

    char filepath[48];
    snprintf(filepath, sizeof(filepath), "/user/%s", filename);

    if (req->content_len == 0) {
        ESP_LOGI(TAG, "Deleting file: %s", filepath);
        unlink(filepath);
        httpd_resp_set_status(req, "204");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    FILE *f = fopen(filepath, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s for writing", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, NULL);
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

    httpd_resp_set_status(req, "201");
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
    .uri      = "/lua/run/*",
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
    RETURN_ON_NULL(ws_buffer_mutex = xSemaphoreCreateMutex(), ESP_ERR_NO_MEM);

    xTaskCreate(ws_send_task, "ws_send_task", 2048, NULL, 5, &ws_send_task_handle);
    RETURN_ON_NULL(ws_send_task_handle, ESP_ERR_NO_MEM);

    httpd_handle_t server = NULL;
    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_cfg.max_uri_handlers = 14;
    httpd_cfg.uri_match_fn = httpd_uri_match_wildcard;
    httpd_cfg.close_fn = httpd_close_cb;

    RETURN_ON_ERROR(httpd_start(&server, &httpd_cfg));

    httpd_register_uri_handler(server, &get_root_uri);
    httpd_register_uri_handler(server, &get_js_uri);
    httpd_register_uri_handler(server, &get_css_uri);
    httpd_register_uri_handler(server, &get_config_uri);
    httpd_register_uri_handler(server, &put_config_uri);
    httpd_register_uri_handler(server, &ws_uri);
    httpd_register_uri_handler(server, &get_wifi_scan_uri);
    httpd_register_uri_handler(server, &get_status_uri);
    #ifdef TASK_LIST
    httpd_register_uri_handler(server, &get_task_list);
    #endif
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
