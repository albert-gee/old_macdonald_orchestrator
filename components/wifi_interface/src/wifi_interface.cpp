#include "wifi_interface.h"
#include <cstring>
#include <esp_check.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <nvs.h>

const char *TAG = "WIFI_INTERFACE";
const char *WIFI_AP_SSID = "Old_MacDonald AP";
const char *WIFI_AP_PASS = "123456789";

#define WIFI_NAMESPACE "wifi_config"

static esp_err_t save_wifi_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    nvs_set_str(nvs_handle, "ssid", ssid);
    nvs_set_str(nvs_handle, "password", password);
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Wi-Fi credentials saved to NVS");
    return err;
}

static esp_err_t get_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return err;

    if (nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len) != ESP_OK ||
        nvs_get_str(nvs_handle, "password", password, &pass_len) != ESP_OK) {
        ESP_LOGW(TAG, "No Wi-Fi credentials found in NVS");
        nvs_close(nvs_handle);
        return ESP_FAIL;
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}

esp_err_t wifi_connect_sta_to_ap(const char *ssid, const char *password) {
    ESP_RETURN_ON_ERROR(esp_wifi_disconnect(), TAG, "Failed to disconnect WiFi");

    wifi_config_t wifi_sta_config = {};
    strncpy((char *)wifi_sta_config.sta.ssid, ssid, sizeof(wifi_sta_config.sta.ssid) - 1);
    strncpy((char *)wifi_sta_config.sta.password, password, sizeof(wifi_sta_config.sta.password) - 1);
    wifi_sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config), TAG, "Failed to set WiFi configuration");
    ESP_RETURN_ON_ERROR(save_wifi_credentials(ssid, password), TAG, "Failed to save Wi-Fi credentials");

    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "Failed to connect WiFi");

    return ESP_OK;
}

esp_err_t wifi_set_ap_sta() {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    // ESP_ERROR_CHECK(esp_wifi_stop());

    esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {};
    strncpy(reinterpret_cast<char *>(wifi_ap_config.ap.ssid), WIFI_AP_SSID, sizeof(wifi_ap_config.ap.ssid) - 1);
    strncpy(reinterpret_cast<char *>(wifi_ap_config.ap.password), WIFI_AP_PASS, sizeof(wifi_ap_config.ap.password) - 1);
    wifi_ap_config.ap.ssid_len = strlen(WIFI_AP_SSID);
    wifi_ap_config.ap.max_connection = 4;
    wifi_ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    // Try to get STA credentials from NVS
    char stored_ssid[32] = {};
    char stored_password[64] = {};

    if (get_wifi_credentials(stored_ssid, sizeof(stored_ssid), stored_password, sizeof(stored_password)) == ESP_OK) {
        ESP_LOGI(TAG, "Using stored credentials for STA mode");
        wifi_config_t wifi_sta_config = {};
        strncpy(reinterpret_cast<char *>(wifi_sta_config.sta.ssid), stored_ssid, sizeof(wifi_sta_config.sta.ssid) - 1);
        strncpy(reinterpret_cast<char *>(wifi_sta_config.sta.password), stored_password, sizeof(wifi_sta_config.sta.password) - 1);
        wifi_sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));
    } else {
        ESP_LOGW(TAG, "No stored credentials, STA mode will not connect");
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}
