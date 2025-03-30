#include "events/thread_event_handler.h"
#include "events/event_handler_util.h"

#include "thread_util.h"
#include "json/json_response.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_openthread_lock.h>
#include <esp_openthread_types.h>
#include <portmacro.h>

static const char *TAG = "THREAD_EVENT_HANDLER";

void handle_thread_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "OpenThread EVENT: %d", event_id);

    esp_err_t err;

    otOperationalDataset dataset;

    switch (event_id) {
        // Dataset Changed
        case OPENTHREAD_EVENT_DATASET_CHANGED:
            ESP_LOGI(TAG, "OpenThread dataset changed");

            // Acquire OpenThread lock
            esp_openthread_lock_acquire(portMAX_DELAY);

            // Get the active dataset, convert it to json, and broadcast it
            err = thread_get_active_dataset(&dataset);
            if (err == ESP_OK) {
                // 2 characters per byte + null terminator
                char mesh_local_prefix_hex[2 * OT_MESH_LOCAL_PREFIX_SIZE + 1] = {};
                binary_to_hex_string(
                    dataset.mMeshLocalPrefix.m8,
                    OT_MESH_LOCAL_PREFIX_SIZE,
                    mesh_local_prefix_hex,
                    sizeof(mesh_local_prefix_hex));

                const char *response = create_json_set_dataset_response(
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

                broadcast_info_message(response);

            } else {
                ESP_LOGE(TAG, "Failed to get device role name");
            }

            esp_openthread_lock_release();

            break;

        // Role Changed
        case OPENTHREAD_EVENT_ROLE_CHANGED:
            ESP_LOGI(TAG, "OpenThread role changed");

            char device_role_name[32];
            err = thread_get_device_role_name(device_role_name);
            if (err == ESP_OK) {
                char message[64];
                sprintf(message, "Device role changed to: %s", device_role_name);
                broadcast_info_message(message);
            } else {
                ESP_LOGE(TAG, "Failed to get device role name");
            }
            break;

        case OPENTHREAD_EVENT_START:
            ESP_LOGI(TAG, "OpenThread stack started");
            break;

        case OPENTHREAD_EVENT_STOP:
            ESP_LOGI(TAG, "OpenThread stack stopped");
            broadcast_info_message("OpenThread stack stopped");
            break;

        case OPENTHREAD_EVENT_DETACHED:
            ESP_LOGI(TAG, "OpenThread detached");
            broadcast_info_message("OpenThread detached");
            break;

        case OPENTHREAD_EVENT_ATTACHED:
            ESP_LOGI(TAG, "OpenThread attached");
            broadcast_info_message("OpenThread attached");
            break;

        case OPENTHREAD_EVENT_IF_UP:
            ESP_LOGI(TAG, "OpenThread network interface up");
            broadcast_info_message("OpenThread network interface up");
            break;

        case OPENTHREAD_EVENT_IF_DOWN:
            ESP_LOGI(TAG, "OpenThread network interface down");
            broadcast_info_message("OpenThread network interface down");
            break;

        case OPENTHREAD_EVENT_GOT_IP6:
            ESP_LOGI(TAG, "OpenThread stack added IPv6 address");
            broadcast_info_message("OpenThread stack added IPv6 address");
            break;

        case OPENTHREAD_EVENT_LOST_IP6:
            ESP_LOGI(TAG, "OpenThread stack removed IPv6 address");
            broadcast_info_message("OpenThread stack removed IPv6 address");
            break;

        case OPENTHREAD_EVENT_MULTICAST_GROUP_JOIN:
            ESP_LOGI(TAG, "OpenThread stack joined IPv6 multicast group");
            broadcast_info_message("OpenThread stack joined IPv6 multicast group");
            break;

        case OPENTHREAD_EVENT_MULTICAST_GROUP_LEAVE:
            ESP_LOGI(TAG, "OpenThread stack left IPv6 multicast group");
            broadcast_info_message("OpenThread stack left IPv6 multicast group");
            break;

        case OPENTHREAD_EVENT_TREL_ADD_IP6:
            ESP_LOGI(TAG, "OpenThread stack added TREL IPv6 address");
            broadcast_info_message("OpenThread stack added TREL IPv6 address");
            break;

        case OPENTHREAD_EVENT_TREL_REMOVE_IP6:
            ESP_LOGI(TAG, "OpenThread stack removed TREL IPv6 address");
            broadcast_info_message("OpenThread stack removed TREL IPv6 address");
            break;

        case OPENTHREAD_EVENT_TREL_MULTICAST_GROUP_JOIN:
            ESP_LOGI(TAG, "OpenThread stack joined TREL IPv6 multicast group");
            broadcast_info_message("OpenThread stack joined TREL IPv6 multicast group");
            break;

        case OPENTHREAD_EVENT_SET_DNS_SERVER:
            ESP_LOGI(TAG, "OpenThread stack set DNS server");
            broadcast_info_message("OpenThread stack set DNS server");
            break;

        case OPENTHREAD_EVENT_PUBLISH_MESHCOP_E:
            ESP_LOGI(TAG, "OpenThread stack started publishing meshcop-e service");
            broadcast_info_message("OpenThread stack started publishing meshcop-e service");
            break;

        case OPENTHREAD_EVENT_REMOVE_MESHCOP_E:
            ESP_LOGI(TAG, "OpenThread stack removed meshcop-e service");
            broadcast_info_message("OpenThread stack removed meshcop-e service");
            break;

        default:
            ESP_LOGW(TAG, "Unknown OpenThread event: %d", event_id);
            break;

    }
}
