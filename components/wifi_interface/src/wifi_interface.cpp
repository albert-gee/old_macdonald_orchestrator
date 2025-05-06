#include "wifi_interface.h"
#include "wifi_nvs_util.h"
#include "sdkconfig.h"

#include <esp_check.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <cstring>

static const char *TAG = "WIFI_INTERFACE";

static esp_netif_t *wifi_ap_netif = nullptr;  /**< Pointer to the AP netif instance */
static esp_netif_t *wifi_sta_netif = nullptr; /**< Pointer to the STA netif instance */

/**
 * @brief Configure Wi-Fi STA mode with given credentials.
 *
 * Constructs a wifi_config_t structure, copies and null-terminates the
 * SSID and password, enforces WPA2/WPA3-PSK authentication, and commits
 * the configuration to the Wi-Fi driver.
 *
 * @param ssid      Null-terminated SSID string.
 * @param password  Null-terminated password string.
 * @return ESP_OK on success, or an esp_err_t error code.
 */
static esp_err_t set_wifi_sta_config(const char *ssid, const char *password) {
    ESP_LOGI(TAG, "Setting Wi‑Fi STA configuration");

    // Prepare an empty configuration structure
    wifi_config_t wifi_sta_config = {};

    // Copy SSID and ensure null-termination
    strncpy(reinterpret_cast<char *>(wifi_sta_config.sta.ssid),
            ssid,
            sizeof(wifi_sta_config.sta.ssid) - 1);
    wifi_sta_config.sta.ssid[sizeof(wifi_sta_config.sta.ssid) - 1] = '\0';

    // Copy password and ensure null-termination
    strncpy(reinterpret_cast<char *>(wifi_sta_config.sta.password),
            password,
            sizeof(wifi_sta_config.sta.password) - 1);
    wifi_sta_config.sta.password[sizeof(wifi_sta_config.sta.password) - 1] = '\0';

    // Use WPA2/WPA3 PSK for STA authentication
    wifi_sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK;

    // Commit the configuration to the driver
    return esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config);
}

/**
 * @brief Load stored STA credentials from NVS and apply STA configuration.
 *
 * Retrieves the SSID and password saved in non-volatile storage. If
 * retrieval succeeds, applies the configuration via set_wifi_sta_config().
 *
 * @return ESP_OK if credentials were found and applied, or an error code.
 */
static esp_err_t set_wifi_sta_config_from_nvs() {
    char stored_sta_ssid[32] = {};
    char stored_sta_password[64] = {};

    // Retrieve saved credentials
    esp_err_t err = get_wifi_sta_credentials(stored_sta_ssid,
                                             sizeof(stored_sta_ssid),
                                             stored_sta_password,
                                             sizeof(stored_sta_password));
    if (err == ESP_OK) {
        // Apply the retrieved configuration
        err = set_wifi_sta_config(stored_sta_ssid, stored_sta_password);
    }
    return err;
}

/**
 * @brief Configure Wi-Fi AP mode with given credentials.
 *
 * Sets connection parameters, chooses the appropriate authentication mode,
 * and commits the configuration.
 *
 * @param ap_ssid      Null-terminated AP SSID string.
 * @param ap_password  Null-terminated AP password string.
 * @return ESP_OK on success, or an esp_err_t error code.
 */
static esp_err_t set_wifi_ap_config(const char *ap_ssid, const char *ap_password) {
    ESP_LOGI(TAG, "Setting Wi‑Fi AP configuration");

    wifi_config_t wifi_ap_config = {};

    // Copy AP SSID and ensure null-termination
    strncpy(reinterpret_cast<char *>(wifi_ap_config.ap.ssid),
            ap_ssid,
            sizeof(wifi_ap_config.ap.ssid) - 1);
    wifi_ap_config.ap.ssid[sizeof(wifi_ap_config.ap.ssid) - 1] = '\0';

    // Copy AP password and ensure null-termination
    strncpy(reinterpret_cast<char *>(wifi_ap_config.ap.password),
            ap_password,
            sizeof(wifi_ap_config.ap.password) - 1);
    wifi_ap_config.ap.password[sizeof(wifi_ap_config.ap.password) - 1] = '\0';

    // Set SSID length explicitly
    wifi_ap_config.ap.ssid_len = strlen(reinterpret_cast<const char *>(wifi_ap_config.ap.ssid));

    // Allow up to 4 simultaneous clients
    wifi_ap_config.ap.max_connection = 4;

    // Choose authentication mode based on password length
    size_t pass_len = strlen(ap_password);
    wifi_ap_config.ap.authmode = (pass_len >= 8)
                                     ? WIFI_AUTH_WPA2_WPA3_PSK
                                     : WIFI_AUTH_OPEN;

    // Commit the configuration to the driver
    return esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config);
}

/**
 * @brief Load stored AP credentials from NVS or use defaults, then apply configuration.
 *
 * Attempts to fetch saved AP SSID and password. On success, applies them;
 * otherwise falls back to default values defined in sdkconfig.
 *
 * @return ESP_OK on success, or an esp_err_t error code.
 */
static esp_err_t set_wifi_ap_config_from_nvs() {
    char stored_ap_ssid[32] = {};
    char stored_ap_password[64] = {};

    esp_err_t err = get_wifi_ap_credentials(stored_ap_ssid,
                                            sizeof(stored_ap_ssid),
                                            stored_ap_password,
                                            sizeof(stored_ap_password));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Using stored AP credentials");
        err = set_wifi_ap_config(stored_ap_ssid, stored_ap_password);
    } else {
        ESP_LOGI(TAG, "Using default AP credentials");
        err = set_wifi_ap_config(CONFIG_WIFI_AP_SSID_DEFAULT,
                                  CONFIG_WIFI_AP_PASS_DEFAULT);
    }
    return err;
}

esp_err_t wifi_interface_init(const esp_event_handler_t event_handler) {
    ESP_LOGI(TAG, "Initializing Wi‑Fi interface");

    // Register provided handler for all Wi-Fi events
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, nullptr);

    // Lazily create AP netif if not already created
    if (!wifi_ap_netif) {
        ESP_LOGI(TAG, "Creating netif for Wi‑Fi AP");
        wifi_ap_netif = esp_netif_create_default_wifi_ap();
        if (!wifi_ap_netif) {
            ESP_LOGE(TAG, "Failed to create Wi‑Fi AP netif");
            return ESP_FAIL;
        }
    }

    // Lazily create STA netif if not already created
    if (!wifi_sta_netif) {
        ESP_LOGI(TAG, "Creating netif for Wi‑Fi STA");
        wifi_sta_netif = esp_netif_create_default_wifi_sta();
        if (!wifi_sta_netif) {
            ESP_LOGE(TAG, "Failed to create Wi‑Fi STA netif");
            return ESP_FAIL;
        }
    }

    // Initialize the Wi-Fi driver with the default configuration
    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    return esp_wifi_init(&cfg);
}

esp_err_t wifi_interface_start() {
    ESP_LOGI(TAG, "Starting Wi-Fi");

    // Stop Wi‑Fi if already running to apply new settings
    ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "Failed to stop Wi‑Fi");

    // Set the Wi-Fi operating mode
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "Mode set failed");

    // Attempt to load STA credentials; default to AP-only if unavailable
    esp_err_t err = set_wifi_sta_config_from_nvs();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No stored STA credentials.");
    } else {
        ESP_LOGI(TAG, "Using stored STA credentials.");
    }

    // Load AP credentials or defaults
    ESP_RETURN_ON_ERROR(set_wifi_ap_config_from_nvs(), TAG, "Failed to set AP config");


    // Start the Wi-Fi driver and bring interfaces up
    ESP_LOGI(TAG, "Starting Wi‑Fi driver");
    return esp_wifi_start();
}

esp_err_t wifi_sta_connect(const char *ssid, const char *password) {
    // Persist new credentials
    ESP_RETURN_ON_ERROR(save_wifi_sta_credentials(ssid, password),
                        TAG, "Save STA creds failed");

    // Disconnect any existing connection
    ESP_RETURN_ON_ERROR(esp_wifi_disconnect(), TAG, "Disconnect failed");

    // Apply the new STA configuration
    ESP_RETURN_ON_ERROR(set_wifi_sta_config(ssid, password),
                        TAG, "Set STA config failed");

    // Initiate connection attempt
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "Connect failed");

    return ESP_OK;
}

esp_err_t wifi_ap_set_credentials(const char *ap_ssid, const char *ap_password) {
    // Persist new AP credentials
    ESP_RETURN_ON_ERROR(save_wifi_ap_credentials(ap_ssid, ap_password),
                        TAG, "Save AP creds failed");
    // Restart interface to apply updated AP settings
    return wifi_interface_start();
}
