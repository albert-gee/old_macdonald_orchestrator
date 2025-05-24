#include "event_handlers/thread_event_handler.h"
#include "messages/outbound_message_builder.h"
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

    switch (event_id) {
        case OPENTHREAD_EVENT_START:
            broadcast_info_thread_stack_status_message(true);
            break;

        case OPENTHREAD_EVENT_STOP:
            broadcast_info_thread_stack_status_message(false);
            break;

        case OPENTHREAD_EVENT_IF_UP:
            broadcast_info_thread_interface_status_message(true);
            break;

        case OPENTHREAD_EVENT_IF_DOWN:
            broadcast_info_thread_interface_status_message(false);
            break;

        case OPENTHREAD_EVENT_ATTACHED:
            broadcast_info_thread_attachment_status_message(true);
            break;

        case OPENTHREAD_EVENT_DETACHED:
            broadcast_info_thread_attachment_status_message(false);
            break;

                case OPENTHREAD_EVENT_ROLE_CHANGED: {
            const char *role_str = nullptr;
            if (thread_get_device_role_string(&role_str) == ESP_OK && role_str != nullptr) {
                broadcast_info_thread_role_message(role_str);
            } else {
                ESP_LOGW(TAG, "Failed to get Thread role string");
            }
            break;
        }

        case OPENTHREAD_EVENT_GOT_IP6:
        case OPENTHREAD_EVENT_LOST_IP6: {
            char *addresses[THREAD_ADDRESS_LIST_MAX] = {nullptr};
            size_t count = 0;

            if (thread_get_unicast_addresses(addresses, THREAD_ADDRESS_LIST_MAX, &count) == ESP_OK) {
                broadcast_info_unicast_addresses_message(const_cast<const char **>(addresses), count);
                thread_free_address_list(addresses, count);
            } else {
                ESP_LOGW(TAG, "Failed to get unicast addresses");
            }
            break;
        }

        case OPENTHREAD_EVENT_MULTICAST_GROUP_JOIN:
        case OPENTHREAD_EVENT_MULTICAST_GROUP_LEAVE: {
            char *addresses[THREAD_ADDRESS_LIST_MAX] = {nullptr};
            size_t count = 0;

            if (thread_get_multicast_addresses(addresses, THREAD_ADDRESS_LIST_MAX, &count) == ESP_OK) {
                broadcast_info_multicast_addresses_message(const_cast<const char **>(addresses), count);
                thread_free_address_list(addresses, count);
            } else {
                ESP_LOGW(TAG, "Failed to get multicast addresses");
            }
            break;
        }

        case OPENTHREAD_EVENT_PUBLISH_MESHCOP_E:
            broadcast_info_meshcop_service_status_message(true);
            break;

        case OPENTHREAD_EVENT_REMOVE_MESHCOP_E:
            broadcast_info_meshcop_service_status_message(false);
            break;

        case OPENTHREAD_EVENT_DATASET_CHANGED: {
            otOperationalDataset dataset;
            if (thread_get_active_dataset(&dataset) == ESP_OK) {
                broadcast_info_active_dataset_message(
                    dataset.mActiveTimestamp.mSeconds,
                    (const char *)dataset.mNetworkName.m8,
                    dataset.mExtendedPanId.m8,
                    dataset.mMeshLocalPrefix.m8,
                    dataset.mPanId,
                    dataset.mChannel
                );
            } else {
                ESP_LOGW(TAG, "Failed to get active dataset");
            }
            break;
        }

        default:
            ESP_LOGI(TAG, "Unhandled OpenThread event: %d", event_id);
            break;
    }
}
