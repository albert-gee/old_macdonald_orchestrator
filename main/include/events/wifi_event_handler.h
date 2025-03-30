#ifndef WIFI_EVENT_HANDLER_H
#define WIFI_EVENT_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_event_base.h>

void handle_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

#ifdef __cplusplus
}
#endif


#endif //WIFI_EVENT_HANDLER_H
