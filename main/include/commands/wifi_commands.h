#ifndef WIFI_COMMANDS_H
#define WIFI_COMMANDS_H

#include <esp_event.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initiates a connection to a Wi-Fi network as a station.
 *
 * @param ssid The SSID of the Wi-Fi network to connect to.
 * @param password The password of the Wi-Fi network.
 * @return ESP_OK on successful initiation of the connect process, or an error code indicating the failure reason.
 */
esp_err_t execute_wifi_sta_connect_command(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif


#endif //WIFI_COMMANDS_H
