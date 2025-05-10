#include "commands/wifi_commands.h"
#include "wifi_interface.h"

esp_err_t execute_wifi_sta_connect_command(const char *ssid, const char *password) {
    return wifi_sta_connect(ssid, password);
}
