#ifndef WIFI_INTERFACE_H
#define WIFI_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>

esp_err_t connect_to_wifi(const char *ssid, const char *password);

esp_err_t start_wifi_ap(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif

#endif // WIFI_INTERFACE_H
