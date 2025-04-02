#include "events/thread_event_handler.h"
#include "json/json_response.h"
#include "thread_util.h"

#include <esp_log.h>
#include <esp_openthread_types.h>
#include <portmacro.h>

static const char *TAG = "THREAD_EVENT_HANDLER";

void handle_thread_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

    if (event_base != OPENTHREAD_EVENT) {
        ESP_LOGE(TAG, "Invalid event base");
        return;
    }

    switch (event_id) {
        case OPENTHREAD_EVENT_DATASET_CHANGED:
            ESP_LOGI(TAG, "OpenThread dataset changed");
            break;
        case OPENTHREAD_EVENT_IF_UP:
            ESP_LOGI(TAG, "OpenThread interface up");
            break;
        case OPENTHREAD_EVENT_START:
            ESP_LOGI(TAG, "OpenThread stack started");
            break;
        default:
            ESP_LOGI(TAG, "Unknown OpenThread event: %d", event_id);
            break;
    }
}
