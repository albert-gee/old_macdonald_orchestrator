#include "wifi_nvs_util.h"

#include <esp_log.h>
#include <nvs.h>

#define WIFI_NAMESPACE "wifi_config"

static const char *TAG = "WIFI_NVS_UTIL";

esp_err_t save_wifi_ap_credentials(const char *ap_ssid, const char *ap_password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    nvs_set_str(nvs_handle, "ap_ssid", ap_ssid);
    nvs_set_str(nvs_handle, "ap_password", ap_password);
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Wi-Fi AP credentials saved to NVS");
    return err;
}

esp_err_t save_wifi_sta_credentials(const char *sta_ssid, const char *sta_password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    nvs_set_str(nvs_handle, "sta_ssid", sta_ssid);
    nvs_set_str(nvs_handle, "sta_password", sta_password);
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Wi-Fi STA credentials saved to NVS");
    return err;
}

esp_err_t get_wifi_ap_credentials(char *ap_ssid, size_t ap_ssid_len, char *ap_password, size_t ap_pass_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return err;

    if (nvs_get_str(nvs_handle, "ap_ssid", ap_ssid, &ap_ssid_len) != ESP_OK ||
        nvs_get_str(nvs_handle, "ap_password", ap_password, &ap_pass_len) != ESP_OK) {
        ESP_LOGW(TAG, "No Wi-Fi AP credentials found in NVS");
        nvs_close(nvs_handle);
        return ESP_FAIL;
        }

    nvs_close(nvs_handle);
    return ESP_OK;
}

esp_err_t get_wifi_sta_credentials(char *sta_ssid, size_t sta_ssid_len, char *sta_password, size_t sta_pass_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return err;

    if (nvs_get_str(nvs_handle, "sta_ssid", sta_ssid, &sta_ssid_len) != ESP_OK ||
        nvs_get_str(nvs_handle, "sta_password", sta_password, &sta_pass_len) != ESP_OK) {
        ESP_LOGW(TAG, "No Wi-Fi STA credentials found in NVS");
        nvs_close(nvs_handle);
        return ESP_FAIL;
        }

    nvs_close(nvs_handle);
    return ESP_OK;
}