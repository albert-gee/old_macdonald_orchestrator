#ifndef WIFI_INTERFACE_H
#define WIFI_INTERFACE_H

#include <esp_err.h>
#include "esp_event_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Wi-Fi interface and register a Wi-Fi event handler.
 *
 * This function sets up the default network interfaces for both Access Point
 * (AP) and Station (STA) modes, registers the provided event handler for
 * all Wi-Fi events, and initializes the Wi-Fi driver with default parameters.
 *
 * @param event_handler Callback function to handle Wi-Fi events.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t wifi_interface_init(esp_event_handler_t event_handler);

/**
 * @brief Start the Wi-Fi driver and bring up network interfaces.
 *
 * This function loads stored STA and AP (default if not stored) credentials,
 * configures the operating mode (AP or AP+STA), and starts the Wi-Fi driver.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t wifi_interface_start(void);

/**
 * @brief Connect Wi-Fi STA to a network and persist credentials.
 *
 * This function saves the provided SSID and password to non-volatile
 * storage (NVS), applies the new STA configuration, and attempts to
 * establish a connection to the specified network.
 *
 * @param ssid      Null-terminated string of the Wi-Fi network SSID.
 * @param password  Null-terminated string of the Wi-Fi network password.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t wifi_sta_connect(const char *ssid, const char *password);

/**
 * @brief Restart Wi-Fi with new AP credentials
 *
 * This function saves the provided AP SSID and password to non-volatile
 * storage (NVS), reconfigures the AP interface, and restarts the Wi-Fi
 * driver to apply the configuration changes.
 *
 * @param ap_ssid      Null-terminated string of the AP SSID.
 * @param ap_password  Null-terminated string of the AP password.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t wifi_ap_set_credentials(const char *ap_ssid, const char *ap_password);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_INTERFACE_H */
