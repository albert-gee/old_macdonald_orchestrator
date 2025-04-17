#ifndef WIFI_INTERFACE_H
#define WIFI_INTERFACE_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Update STA credentials in the Wi‑Fi driver (volatile only).
 *
 * Applies the given SSID and password to the driver but does not save them
 * to NVS and does not initiate a (re)connection.
 *
 * @param ssid      Null‑terminated SSID (1–32 bytes).
 * @param password  Null‑terminated passphrase (0–63 bytes); use an empty
 *                  string for open networks.
 *
 * @return ESP_OK on success, otherwise the esp_wifi_set_config() error code.
 */
esp_err_t set_wifi_sta_config(const char *ssid, const char *password);


/**
 * @brief Configure access‑point parameters in the driver.
 *
 * If @p ap_password is shorter than eight characters, the AP is set to
 * WIFI_AUTH_OPEN; otherwise WPA2/WPA3‑PSK is enforced. This call neither
 * starts the AP nor persists the credentials.
 *
 * @param ap_ssid     Null‑terminated SSID for the AP (1–32 bytes).
 * @param ap_password Null‑terminated passphrase (0–63 bytes).
 *
 * @return ESP_OK on success, or an error from ::esp_wifi_set_config.
 */
esp_err_t set_wifi_ap_config(const char *ap_ssid, const char *ap_password);

/**
 * @brief Initialise the ESP‑IDF Wi‑Fi driver and create default netifs.
 *
 * Must be invoked once after boot—before any other Wi‑Fi functions.
 *
 * @return ESP_OK on success, or an esp_err_t describing the failure.
 */
esp_err_t wifi_interface_init(void);

/**
 * @brief Start Wi‑Fi in simultaneous AP + STA mode.
 *
 * Loads credentials from NVS (or compile‑time defaults), applies them to the
 * driver, and starts the Wi‑Fi stack.
 *
 * @return ESP_OK on success, or an esp_err_t describing the failure.
 */
esp_err_t start_wifi_ap_sta(void);

/**
 * @brief Persist new station credentials, update the driver, and reconnect.
 *
 * @param ssid      New SSID.
 * @param password  New passphrase.
 *
 * @return ESP_OK on success, or an esp_err_t describing the failure.
 */
esp_err_t wifi_sta_connect(const char *ssid, const char *password);

/**
 * @brief Persist new AP credentials and restart Wi‑Fi so changes take effect.
 *
 * Internally saves to NVS, then calls ::start_wifi_ap_sta().
 *
 * @param ap_ssid     New AP SSID.
 * @param ap_password New AP passphrase.
 *
 * @return ESP_OK on success, or an esp_err_t describing the failure.
 */
esp_err_t wifi_ap_set_credentials(const char *ap_ssid, const char *ap_password);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_INTERFACE_H */
