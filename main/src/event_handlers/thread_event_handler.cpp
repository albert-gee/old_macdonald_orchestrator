#include "event_handlers/thread_event_handler.h"
#include "messages/outbound_message_builder.h"
#include "state/orchestrator_state.h"
#include "thread_util.h"

#include <esp_log.h>
#include <esp_openthread_types.h>
#include <portmacro.h>
#include <cJSON.h>
#include <cstring>

static const char *TAG = "THREAD_EVENT_HANDLER";

void handle_thread_event(void *arg, const esp_event_base_t event_base, const int32_t event_id, void *event_data) {
    if (event_base != OPENTHREAD_EVENT) {
        ESP_LOGE(TAG, "Invalid event base");
        return;
    }

    switch (event_id) {
        case OPENTHREAD_EVENT_START: {
            bool running = false;
            if (thread_is_stack_running(&running) != ESP_OK) running = false;
            orchestrator_state_set_thread_enabled(running);
            if (running) orchestrator_state_broadcast_event("thread.enabled", nullptr);
            broadcast_info_thread_stack_status_message(running);
            orchestrator_state_broadcast_snapshot();
            break;
        }

        case OPENTHREAD_EVENT_STOP:
            orchestrator_state_set_thread_enabled(false);
            orchestrator_state_broadcast_event("thread.disabled", nullptr);
            broadcast_info_thread_stack_status_message(false);
            orchestrator_state_broadcast_snapshot();
            break;

        case OPENTHREAD_EVENT_IF_UP:
            broadcast_info_thread_interface_status_message(true);
            break;

        case OPENTHREAD_EVENT_IF_DOWN:
            broadcast_info_thread_interface_status_message(false);
            break;

        case OPENTHREAD_EVENT_ATTACHED:
            orchestrator_state_set_thread_attached(true);
            orchestrator_state_broadcast_event("thread.attached", nullptr);
            broadcast_info_thread_attachment_status_message(true);
            orchestrator_state_broadcast_snapshot();
            break;

        case OPENTHREAD_EVENT_DETACHED:
            orchestrator_state_set_thread_attached(false);
            orchestrator_state_broadcast_event("thread.detached", nullptr);
            broadcast_info_thread_attachment_status_message(false);
            orchestrator_state_broadcast_snapshot();
            break;

        case OPENTHREAD_EVENT_ROLE_CHANGED: {
            const char *role_str = nullptr;
            if (thread_get_device_role_string(&role_str) == ESP_OK && role_str != nullptr) {
                orchestrator_state_set_thread_enabled(strcmp(role_str, "disabled") != 0);
                orchestrator_state_set_thread_role(role_str);
                cJSON *payload = cJSON_CreateObject();
                if (payload) cJSON_AddStringToObject(payload, "role", role_str);
                orchestrator_state_broadcast_event("thread.role_changed", payload);
                broadcast_info_thread_role_message(role_str);
                orchestrator_state_broadcast_snapshot();
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
            auto *dataset_event = static_cast<esp_openthread_dataset_changed_event_t *>(event_data);
            if (!dataset_event) {
                ESP_LOGW(TAG, "Dataset changed event missing payload");
                break;
            }

            if (dataset_event->type != OPENTHREAD_ACTIVE_DATASET) {
                ESP_LOGI(TAG, "Ignoring pending dataset change event");
                break;
            }

            if (dataset_event->new_dataset.mComponents.mIsNetworkNamePresent &&
                dataset_event->new_dataset.mComponents.mIsExtendedPanIdPresent &&
                dataset_event->new_dataset.mComponents.mIsMeshLocalPrefixPresent &&
                dataset_event->new_dataset.mComponents.mIsPanIdPresent &&
                dataset_event->new_dataset.mComponents.mIsChannelPresent) {
                const otOperationalDataset &dataset = dataset_event->new_dataset;
                orchestrator_state_set_thread_dataset_present(true);
                orchestrator_state_broadcast_event("thread.dataset_changed", nullptr);
                broadcast_info_active_dataset_message(
                    dataset.mActiveTimestamp.mSeconds,
                    (const char *)dataset.mNetworkName.m8,
                    dataset.mExtendedPanId.m8,
                    dataset.mMeshLocalPrefix.m8,
                    dataset.mPanId,
                    dataset.mChannel
                );
                orchestrator_state_broadcast_snapshot();
            } else {
                orchestrator_state_set_thread_dataset_present(false);
                ESP_LOGW(TAG, "Active dataset changed, but no complete active dataset was provided");
                orchestrator_state_broadcast_snapshot();
            }
            break;
        }

        default:
            ESP_LOGI(TAG, "Unhandled OpenThread event: %d", event_id);
            break;
    }
}
