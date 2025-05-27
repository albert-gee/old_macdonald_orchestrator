#include "wifi_interface.h"
#include "wifi_nvs_util.h"
#include "sdkconfig.h"
#include <esp_check.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <cstring>

static const char *TAG = "WIFI_INTERFACE";

// Pointer to the Wi-Fi Access Point network interface.
static esp_netif_t *wifi_ap_netif = nullptr;

// Pointer to the Wi-Fi Station network interface.
static esp_netif_t *wifi_sta_netif = nullptr;

// Max length for a Wi-Fi SSID, including the null terminator.
static constexpr size_t WIFI_SSID_MAX_LEN = 32;

// Max length for a Wi-Fi password, including the null terminator.
static constexpr size_t WIFI_PASS_MAX_LEN = 64;

/**
 * @brief Copies a string into a destination buffer with null termination.
 *
 * @param dest A pointer to the destination buffer where the data will be copied.
 * @param src A pointer to the source string to be copied into the destination buffer.
 * @param max_len The maximum size of the destination buffer, including space for null termination.
 */
static void copy_wifi_field(char *dest, const char *src, const size_t max_len) {
    strncpy(dest, src, max_len - 1);
    dest[max_len - 1] = '\0'; // Null termination
}

/**
 * Configures the Wi-Fi station (STA) with the given SSID and password.
 *
 * @param ssid The SSID for the Wi-Fi network. Must be a null-terminated string.
 * @param password The password for the Wi-Fi network. Must be a null-terminated string.
 * @return
 *         - ESP_OK: Successfully configured the Wi-Fi station.
 *         - Other `esp_err_t` codes if the configuration fails.
 */
static esp_err_t configure_sta(const char *ssid, const char *password) {
    ESP_LOGI(TAG, "Configuring Wi‑Fi STA");

    wifi_config_t config = {};
    copy_wifi_field(reinterpret_cast<char *>(config.sta.ssid), ssid, sizeof(config.sta.ssid));
    copy_wifi_field(reinterpret_cast<char *>(config.sta.password), password, sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK;

    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

/**
 * @brief Loads and configures Wi-Fi station (STA) credentials from Non-Volatile Storage (NVS).
 *
 * This function attempts to retrieve the stored Wi-Fi SSID and password for Station mode
 * from NVS. If the credentials are successfully retrieved, it uses them to
 * configure the Wi-Fi STA mode. If the credentials are not found in NVS,
 * it returns an error.
 *
 * @return
 *  - ESP_OK: Successfully configured Wi-Fi STA with credentials from NVS.
 *  - ESP_ERR_NOT_FOUND: Wi-Fi credentials were not found in NVS.
 */
static esp_err_t load_sta_config_from_nvs() {
    char ssid[WIFI_SSID_MAX_LEN] = {};
    char password[WIFI_PASS_MAX_LEN] = {};
    if (get_wifi_sta_credentials(ssid, sizeof(ssid), password, sizeof(password)) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    return configure_sta(ssid, password);
}

/**
 * @brief Configures the device to operate as a Wi-Fi access point (AP).
 *
 * This function sets up the Wi-Fi access point (AP) mode with the specified
 * SSID and password. It also configures additional AP parameters such as
 * authentication mode and maximum number of simultaneous connections.
 *
 * @param ssid The SSID of the Wi-Fi AP. Limited to 32 characters.
 * @param password The password of the Wi-Fi AP. Limited to 64 characters.
 *
 * @return
 * - ESP_OK on successful configuration.
 * - Appropriate error code otherwise.
 */
static esp_err_t configure_ap(const char *ssid, const char *password) {
    ESP_LOGI(TAG, "Configuring Wi‑Fi AP");

    wifi_config_t config = {};
    copy_wifi_field(reinterpret_cast<char *>(config.ap.ssid), ssid, sizeof(config.ap.ssid));
    copy_wifi_field(reinterpret_cast<char *>(config.ap.password), password, sizeof(config.ap.password));
    config.ap.ssid_len = strlen(reinterpret_cast<char *>(config.ap.ssid));
    config.ap.max_connection = 4;
    config.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;

    return esp_wifi_set_config(WIFI_IF_AP, &config);
}

/**
 * @brief Loads Wi-Fi access point (AP) configuration from Non-Volatile Storage (NVS).
 *
 * Attempts to retrieve the AP SSID and password from NVS. If the retrieval is successful,
 * the AP is configured with the loaded credentials. If the retrieval fails, the AP is
 * configured with the default credentials defined in the sdkconfig.
 *
 * @return
 *      - ESP_OK: The AP configuration was successfully set.
 *      - Error code: An error occurred while configuring the AP.
 */
static esp_err_t load_ap_config_from_nvs() {
    char ssid[WIFI_SSID_MAX_LEN] = {};
    char password[WIFI_PASS_MAX_LEN] = {};

    if (get_wifi_ap_credentials(ssid, sizeof(ssid), password, sizeof(password)) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded AP config from NVS");
        return configure_ap(ssid, password);
    }
    ESP_LOGI(TAG, "Using default AP config from sdkconfig");
    return configure_ap(CONFIG_WIFI_AP_SSID_DEFAULT, CONFIG_WIFI_AP_PASS_DEFAULT);
}

esp_err_t wifi_interface_init(const esp_event_handler_t event_handler) {
    ESP_LOGI(TAG, "Initializing Wi‑Fi Interface");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, nullptr),
                        TAG, "Failed to register event handler");

    if (!wifi_ap_netif) {
        wifi_ap_netif = esp_netif_create_default_wifi_ap();
        ESP_RETURN_ON_FALSE(wifi_ap_netif, ESP_FAIL, TAG, "Failed to create AP netif");
    }

    if (!wifi_sta_netif) {
        wifi_sta_netif = esp_netif_create_default_wifi_sta();
        ESP_RETURN_ON_FALSE(wifi_sta_netif, ESP_FAIL, TAG, "Failed to create STA netif");
    }

    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    return esp_wifi_init(&cfg);
}

esp_err_t wifi_interface_start() {
    ESP_LOGI(TAG, "Starting Wi‑Fi");

    ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "Failed to stop Wi‑Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "Failed to set mode");

    if (load_sta_config_from_nvs() == ESP_OK) {
        ESP_LOGI(TAG, "STA credentials loaded from NVS");
    } else {
        ESP_LOGW(TAG, "No valid STA credentials found");
    }

    return load_ap_config_from_nvs();
}

esp_err_t wifi_sta_connect(const char *ssid, const char *password) {
    ESP_RETURN_ON_ERROR(save_wifi_sta_credentials(ssid, password), TAG, "Failed to save STA credentials");

    ESP_LOGI(TAG, "Reconnecting with new STA credentials");
    if (esp_wifi_disconnect() != ESP_OK) {
        ESP_LOGW(TAG, "No existing STA connection to disconnect");
    }

    ESP_RETURN_ON_ERROR(configure_sta(ssid, password), TAG, "Failed to set STA config");
    return esp_wifi_connect();
}

esp_err_t wifi_ap_set_credentials(const char *ap_ssid, const char *ap_password) {
    ESP_RETURN_ON_ERROR(save_wifi_ap_credentials(ap_ssid, ap_password), TAG, "Failed to save AP credentials");
    return wifi_interface_start();
}
