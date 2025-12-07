#ifndef GLOBALS_H
#define GLOBALS_H

#include "esp_http_server.h"

#define MAX_WS_CLIENTS 10

typedef struct {
    httpd_handle_t handle;
    int fd;
} ws_client_info_t;

extern ws_client_info_t ws_clients[MAX_WS_CLIENTS];
extern int ws_clients_count;

#endif // GLOBALS_H
