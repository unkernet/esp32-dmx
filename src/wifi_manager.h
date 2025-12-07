#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"

void wifi_init_sta(const char* ssid, const char* password);
void wifi_init_ap(void);
esp_err_t save_wifi_config(const char* ssid, const char* password);
esp_err_t read_wifi_config(char* ssid, size_t ssid_len, char* password, size_t password_len);

#endif // WIFI_MANAGER_H
