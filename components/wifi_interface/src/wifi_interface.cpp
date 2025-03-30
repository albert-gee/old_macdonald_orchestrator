#include "wifi_interface.h"

#include <cstring>
#include <esp_check.h>
#include <esp_wifi.h>

const char *TAG = "WIFI_INTERFACE";

esp_err_t connect_to_wifi(const char *ssid, const char *password) {

    // ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "Failed to start WiFi");
    // ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start WiFi");

    wifi_config_t wifi_sta_config = {};
    memcpy(wifi_sta_config.sta.ssid, ssid, strlen(ssid));
    memcpy(wifi_sta_config.sta.password, password, strlen(password));
    wifi_sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config), TAG, "Failed to set WiFi configuration");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "Failed to connect WiFi");

    return ESP_OK;
}

esp_err_t start_wifi_ap(const char *ssid, const char *password) {


    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = "ESP32_AP",
            .password = "123456789",
            .max_connection = 4,
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;

}