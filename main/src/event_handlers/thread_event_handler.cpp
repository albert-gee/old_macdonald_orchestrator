#include "event_handlers/thread_event_handler.h"
#include "../../include/messages/json_outbound_message.h"
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
            create_info_thread_status_message("started");
            break;

        case OPENTHREAD_EVENT_DATASET_CHANGED:
            ESP_LOGI(TAG, "OpenThread dataset changed");
            err = thread_get_active_dataset(&dataset);
            if (err == ESP_OK) {
                create_info_thread_dataset_active_message(
                    dataset.mActiveTimestamp.mSeconds,
                    dataset.mPendingTimestamp.mSeconds,
                    dataset.mNetworkKey.m8,
                    dataset.mNetworkName.m8,
                    dataset.mExtendedPanId.m8,
                    dataset.mMeshLocalPrefix.m8,
                    dataset.mDelay,
                    dataset.mPanId,
                    dataset.mChannel,
                    dataset.mWakeupChannel,
                    dataset.mPskc.m8,
                    OT_MESH_LOCAL_PREFIX_SIZE
                );
            } else {
                ESP_LOGE(TAG, "Failed to get active dataset: %s", esp_err_to_name(err));
            }
            break;

        case OPENTHREAD_EVENT_ROLE_CHANGED: {
            char device_role_name[32];
            err = thread_get_device_role_name(device_role_name);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Device role changed: %s", device_role_name);
                create_info_thread_role_message(device_role_name);
            } else {
                ESP_LOGE(TAG, "Failed to get device role name: %s", esp_err_to_name(err));
            }
            break;
        }

        case OPENTHREAD_EVENT_IF_UP:
            ESP_LOGI(TAG, "OpenThread interface up");
            create_info_ifconfig_status_message("enabled");
            break;

        case OPENTHREAD_EVENT_IF_DOWN:
            ESP_LOGI(TAG, "OpenThread interface down");
            create_info_ifconfig_status_message("disabled");
            break;

        case OPENTHREAD_EVENT_ATTACHED:
            ESP_LOGI(TAG, "OpenThread stack attached");
            create_info_thread_status_message("attached");
            break;

        case OPENTHREAD_EVENT_DETACHED:
            ESP_LOGI(TAG, "OpenThread stack detached");
            create_info_thread_status_message("detached");
            break;

        default:
            ESP_LOGI(TAG, "Unknown OpenThread event: %d", event_id);
            break;
    }
}
