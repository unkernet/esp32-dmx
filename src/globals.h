#ifndef GLOBALS_H
#define GLOBALS_H

#include "esp_http_server.h"

typedef struct {
    httpd_handle_t handle;
    int fd;
} ws_client_info_t;

#endif // GLOBALS_H
