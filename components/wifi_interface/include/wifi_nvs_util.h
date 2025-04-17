#ifndef WIFI_NVS_UTIL_H
#define WIFI_NVS_UTIL_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

    esp_err_t save_wifi_ap_credentials(const char *ap_ssid, const char *ap_password);

    esp_err_t save_wifi_sta_credentials(const char *sta_ssid, const char *sta_password);

    esp_err_t get_wifi_ap_credentials(char *ap_ssid, size_t ap_ssid_len, char *ap_password, size_t ap_pass_len);

    esp_err_t get_wifi_sta_credentials(char *sta_ssid, size_t sta_ssid_len, char *sta_password, size_t sta_pass_len);

#ifdef __cplusplus
}
#endif

#endif //WIFI_NVS_UTIL_H
