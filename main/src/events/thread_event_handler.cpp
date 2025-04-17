#include "events/thread_event_handler.h"
#include "json/json_response.h"
#include "thread_util.h"

#include <esp_log.h>
#include <esp_openthread_types.h>
#include <portmacro.h>
#include <utils/event_handler_util.h>

static const char *TAG = "THREAD_EVENT_HANDLER";

void handle_thread_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base != OPENTHREAD_EVENT) {
        ESP_LOGE(TAG, "Invalid event base");
        return;
    }

    esp_err_t err = ESP_OK;
    otOperationalDataset dataset;

    switch (event_id) {
        // OpenThread started
        case OPENTHREAD_EVENT_START:
            ESP_LOGI(TAG, "OpenThread started");
            break;

        // Dataset Changed
        case OPENTHREAD_EVENT_DATASET_CHANGED:
            ESP_LOGI(TAG, "OpenThread dataset changed");
            err = thread_get_active_dataset(&dataset);
            if (err == ESP_OK) {
                // 2 characters per byte + null terminator
                char mesh_local_prefix_hex[2 * OT_MESH_LOCAL_PREFIX_SIZE + 1] = {};
                binary_to_hex_string(
                    dataset.mMeshLocalPrefix.m8,
                    OT_MESH_LOCAL_PREFIX_SIZE,
                    mesh_local_prefix_hex,
                    sizeof(mesh_local_prefix_hex));

                broadcast_thread_dataset_active_message(
                    dataset.mActiveTimestamp.mSeconds, // Pass mSeconds (or another field if appropriate)
                    dataset.mPendingTimestamp.mSeconds, // Pass mSeconds (or another field if appropriate)
                    dataset.mNetworkKey.m8, // Network Key
                    dataset.mNetworkName.m8, // Network Name
                    dataset.mExtendedPanId.m8, // Extended PAN ID
                    mesh_local_prefix_hex, // Mesh Local Prefix
                    dataset.mDelay, // Delay Timer
                    dataset.mPanId, // PAN ID
                    dataset.mChannel, // Channel
                    dataset.mWakeupChannel, // Wake-up Channel
                    dataset.mPskc.m8 // PSKc
                );
            } else {
                ESP_LOGE(TAG, "Failed to get device role name");
            }
            break;

        // Role Changed
        case OPENTHREAD_EVENT_ROLE_CHANGED:
            char device_role_name[32];
            err = thread_get_device_role_name(device_role_name);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Device role name: %s", device_role_name);
                broadcast_thread_role_set_message(device_role_name);
            } else {
                ESP_LOGE(TAG, "Failed to get device role name");
            }
            break;

        case OPENTHREAD_EVENT_IF_UP:
            ESP_LOGI(TAG, "OpenThread interface up");
            broadcast_ifconfig_status("enabled");
            break;
        case OPENTHREAD_EVENT_IF_DOWN:
            ESP_LOGI(TAG, "OpenThread interface down");
            broadcast_ifconfig_status("disabled");
            break;
        case OPENTHREAD_EVENT_ATTACHED:
            ESP_LOGI(TAG, "OpenThread stack started");
            broadcast_thread_status("attached");
            break;
        case OPENTHREAD_EVENT_DETACHED:
            ESP_LOGI(TAG, "OpenThread stack stopped");
            broadcast_thread_status("detached");
            break;

        default:
            ESP_LOGI(TAG, "Unknown OpenThread event: %d", event_id);
            break;
    }
}
