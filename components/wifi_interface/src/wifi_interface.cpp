#include "wifi_interface.h"
#include "wifi_nvs_util.h"

#include <esp_check.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <cstring>
#include "sdkconfig.h"

static const char *TAG = "WIFI_INTERFACE";

static esp_netif_t *wifi_ap_netif  = nullptr;
static esp_netif_t *wifi_sta_netif = nullptr;

esp_err_t set_wifi_sta_config(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Setting Wi‑Fi STA configuration");

    // Create a new Wi‑Fi STA configuration structure
    wifi_config_t wifi_sta_config = {};
    strncpy(reinterpret_cast<char *>(wifi_sta_config.sta.ssid),
            ssid,
            sizeof(wifi_sta_config.sta.ssid) - 1);
    wifi_sta_config.sta.ssid[sizeof(wifi_sta_config.sta.ssid) - 1] = '\0';
    strncpy(reinterpret_cast<char *>(wifi_sta_config.sta.password),
            password,
            sizeof(wifi_sta_config.sta.password) - 1);
    wifi_sta_config.sta.password[sizeof(wifi_sta_config.sta.password) - 1] = '\0';
    wifi_sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK;

    // Commit configuration to the driver
    return esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config);
}

esp_err_t set_wifi_ap_config(const char *ap_ssid, const char *ap_password)
{
    ESP_LOGI(TAG, "Setting Wi‑Fi AP configuration");

    // Create a new Wi‑Fi AP configuration structure
    wifi_config_t wifi_ap_config = {};
    strncpy(reinterpret_cast<char *>(wifi_ap_config.ap.ssid),
            ap_ssid,
            sizeof(wifi_ap_config.ap.ssid) - 1);
    wifi_ap_config.ap.ssid[sizeof(wifi_ap_config.ap.ssid) - 1] = '\0';
    strncpy(reinterpret_cast<char *>(wifi_ap_config.ap.password),
            ap_password,
            sizeof(wifi_ap_config.ap.password) - 1);
    wifi_ap_config.ap.password[sizeof(wifi_ap_config.ap.password) - 1] = '\0';
    wifi_ap_config.ap.ssid_len = strlen(reinterpret_cast<const char *>(wifi_ap_config.ap.ssid));
    wifi_ap_config.ap.max_connection = 4;  // Max simultaneous clients
    size_t pass_len = strlen(ap_password);
    wifi_ap_config.ap.authmode = (pass_len >= 8) ? WIFI_AUTH_WPA2_WPA3_PSK
                                                 : WIFI_AUTH_OPEN;

    // Commit configuration to the driver
    return esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config);
}

esp_err_t wifi_interface_init()
{
    ESP_LOGI(TAG, "Initializing Wi‑Fi interface");

    // Lazily create AP netif once (reuse across restarts)
    if (!wifi_ap_netif) {
        ESP_LOGI(TAG, "Creating netif for Wi‑Fi AP");
        wifi_ap_netif = esp_netif_create_default_wifi_ap();
        if (!wifi_ap_netif) {
            ESP_LOGE(TAG, "Failed to create Wi‑Fi AP netif");
            return ESP_FAIL;
        }
    }

    // Lazily create STA netif once
    if (!wifi_sta_netif) {
        ESP_LOGI(TAG, "Creating netif for Wi‑Fi STA");
        wifi_sta_netif = esp_netif_create_default_wifi_sta();
        if (!wifi_sta_netif) {
            ESP_LOGE(TAG, "Failed to create Wi‑Fi STA netif");
            return ESP_FAIL;
        }
    }

    // Initialize the driver with default parameters
    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    return esp_wifi_init(&cfg);
}

esp_err_t start_wifi_ap_sta()
{
    // Ensure Wi‑Fi is stopped before reconfiguring
    ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "Failed to stop Wi‑Fi");

    // Switch driver into simultaneous AP + STA mode
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA),
                        TAG, "Mode set failed");

    // Configure STA from stored credentials
    char stored_sta_ssid[32]     = {};
    char stored_sta_password[64] = {};
    if (get_wifi_sta_credentials(stored_sta_ssid, sizeof(stored_sta_ssid),
                                 stored_sta_password, sizeof(stored_sta_password)) == ESP_OK) {
        ESP_LOGI(TAG, "Using stored STA credentials");
        ESP_ERROR_CHECK(set_wifi_sta_config(stored_sta_ssid, stored_sta_password));
    } else {
        ESP_LOGI(TAG, "No stored STA credentials");
    }

    // Configure AP from stored credentials, else defaults
    char stored_ap_ssid[32]     = {};
    char stored_ap_password[64] = {};
    if (get_wifi_ap_credentials(stored_ap_ssid, sizeof(stored_ap_ssid),
                                stored_ap_password, sizeof(stored_ap_password)) == ESP_OK) {
        ESP_LOGI(TAG, "Using stored AP credentials");
        set_wifi_ap_config(stored_ap_ssid, stored_ap_password);
    } else {
        ESP_LOGI(TAG, "Using default AP credentials");
        set_wifi_ap_config(CONFIG_WIFI_AP_SSID_DEFAULT, CONFIG_WIFI_AP_PASS_DEFAULT);
    }

    // Bring interfaces up
    ESP_LOGI(TAG, "Starting Wi‑Fi");
    return esp_wifi_start();
}

esp_err_t wifi_sta_connect(const char *ssid, const char *password)
{
    // Persist new credentials to NVS
    ESP_RETURN_ON_ERROR(save_wifi_sta_credentials(ssid, password),
                        TAG, "Save STA creds failed");

    // Disconnect if already connected
    ESP_RETURN_ON_ERROR(esp_wifi_disconnect(), TAG, "Disconnect failed");

    // Apply new configuration
    ESP_RETURN_ON_ERROR(set_wifi_sta_config(ssid, password),
                        TAG, "Set STA config failed");

    // Attempt connection
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "Connect failed");

    return ESP_OK;
}

esp_err_t wifi_ap_set_credentials(const char *ap_ssid, const char *ap_password)
{
    // Persist new AP credentials and restart interfaces
    ESP_RETURN_ON_ERROR(save_wifi_ap_credentials(ap_ssid, ap_password),
                        TAG, "Save AP creds failed");
    return start_wifi_ap_sta();
}
