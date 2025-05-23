#ifndef THREAD_EVENT_HANDLER_H
#define THREAD_EVENT_HANDLER_H

#include <esp_event_base.h>

#ifdef __cplusplus
extern "C" {
#endif

void handle_thread_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

#ifdef __cplusplus
}
#endif

#endif //THREAD_EVENT_HANDLER_H
