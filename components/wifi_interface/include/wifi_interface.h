#ifndef WIFI_INTERFACE_H
#define WIFI_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>

esp_err_t wifi_connect_sta_to_ap(const char *ssid, const char *password);

esp_err_t wifi_set_ap_sta();

#ifdef __cplusplus
}
#endif

#endif // WIFI_INTERFACE_H
