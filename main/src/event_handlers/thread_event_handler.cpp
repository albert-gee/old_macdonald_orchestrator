#include "event_handlers/thread_event_handler.h"
#include "messages/json_outbound_message.h"
#include "thread_util.h"

#include <esp_log.h>
#include <esp_openthread_types.h>
#include <portmacro.h>

static const char *TAG = "THREAD_EVENT_HANDLER";

void handle_thread_event(void *arg, const esp_event_base_t event_base, const int32_t event_id, void *event_data) {
    if (event_base != OPENTHREAD_EVENT) {
        ESP_LOGE(TAG, "Invalid event base");
        return;
    }

    esp_err_t err = ESP_OK;
    otOperationalDataset dataset;

    switch (event_id) {
        case OPENTHREAD_EVENT_START:
            ESP_LOGI(TAG, "OpenThread started");
            create_info_thread_status_message("Running");
            break;

        case OPENTHREAD_EVENT_STOP:
            ESP_LOGI(TAG, "OpenThread stopped");
            create_info_thread_status_message("Stopped");
            break;

        case OPENTHREAD_EVENT_DETACHED:
            ESP_LOGI(TAG, "OpenThread stack detached");
            create_info_thread_status_message("Detached");
            break;

        case OPENTHREAD_EVENT_ATTACHED:
            ESP_LOGI(TAG, "OpenThread stack attached");
            create_info_thread_status_message("Attached");
            break;

        case OPENTHREAD_EVENT_ROLE_CHANGED: {
            char device_role_name[32];
            err = thread_get_device_role_name(device_role_name);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Role changed: %s", device_role_name);
                create_info_thread_role_message(device_role_name);
            }
            break;
        }

        case OPENTHREAD_EVENT_DATASET_CHANGED:
            ESP_LOGI(TAG, "OpenThread dataset changed");
            err = thread_get_active_dataset(&dataset);
            if (err == ESP_OK) {
                create_info_thread_dataset_active_message(
                    dataset.mActiveTimestamp.mSeconds,
                    dataset.mNetworkName.m8,
                    dataset.mExtendedPanId.m8,
                    dataset.mMeshLocalPrefix.m8,
                    dataset.mPanId,
                    dataset.mChannel
                );
            } else {
                ESP_LOGE(TAG, "Failed to get active dataset: %s", esp_err_to_name(err));
            }

        default:
            ESP_LOGI(TAG, "Unhandled OpenThread event: %d", event_id);
            break;
    }
}
