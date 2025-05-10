#ifndef WIFI_COMMANDS_H
#define WIFI_COMMANDS_H

#include <esp_event.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t execute_wifi_sta_connect_command(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif


#endif //WIFI_COMMANDS_H
