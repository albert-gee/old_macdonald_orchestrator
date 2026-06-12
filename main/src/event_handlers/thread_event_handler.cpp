#include "event_handlers/thread_event_handler.h"
#include "messages/outbound_message_builder.h"
#include "state/orchestrator_state.h"
#include "thread_util.h"

#include <esp_log.h>
#include <esp_openthread_types.h>
#include <openthread/dataset.h>
#include <portmacro.h>
#include <cJSON.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

static const char *TAG = "THREAD_EVENT_HANDLER";

static bool active_dataset_complete(const otOperationalDataset &dataset) {
    return dataset.mComponents.mIsNetworkNamePresent &&
           dataset.mComponents.mIsExtendedPanIdPresent &&
           dataset.mComponents.mIsMeshLocalPrefixPresent &&
           dataset.mComponents.mIsPanIdPresent &&
           dataset.mComponents.mIsChannelPresent;
}

static void copy_dataset_network_name(const otOperationalDataset &dataset, char *dest, size_t dest_len) {
    if (!dest || dest_len == 0) return;
    const size_t copy_len = std::min(dest_len - 1, static_cast<size_t>(OT_NETWORK_NAME_MAX_SIZE));
    size_t i = 0;
    for (; i < copy_len && dataset.mNetworkName.m8[i] != '\0'; ++i) {
        const unsigned char ch = static_cast<unsigned char>(dataset.mNetworkName.m8[i]);
        dest[i] = (ch >= 0x20 && ch <= 0x7e) ? static_cast<char>(ch) : '?';
    }
    dest[i] = '\0';
}

static void publish_active_dataset(const otOperationalDataset &dataset) {
    orchestrator_state_set_thread_dataset_present(true);
    char network_name[OT_NETWORK_NAME_MAX_SIZE + 1] = {};
    char ext_pan_id[17] = {};
    char mesh_prefix[48] = {};
    char active_timestamp[32] = {};
    copy_dataset_network_name(dataset, network_name, sizeof(network_name));
    for (int i = 0; i < 8; ++i) {
        snprintf(ext_pan_id + i * 2, 3, "%02X", dataset.mExtendedPanId.m8[i]);
    }
    snprintf(mesh_prefix, sizeof(mesh_prefix), "%02X%02X:%02X%02X:%02X%02X:%02X%02X::/64",
             dataset.mMeshLocalPrefix.m8[0], dataset.mMeshLocalPrefix.m8[1],
             dataset.mMeshLocalPrefix.m8[2], dataset.mMeshLocalPrefix.m8[3],
             dataset.mMeshLocalPrefix.m8[4], dataset.mMeshLocalPrefix.m8[5],
             dataset.mMeshLocalPrefix.m8[6], dataset.mMeshLocalPrefix.m8[7]);
    snprintf(active_timestamp, sizeof(active_timestamp), "%llu",
             static_cast<unsigned long long>(dataset.mActiveTimestamp.mSeconds));
    orchestrator_state_set_thread_dataset_fields(
        network_name,
        dataset.mChannel,
        dataset.mPanId,
        ext_pan_id,
        mesh_prefix,
        active_timestamp,
        nullptr);
    orchestrator_state_broadcast_event("thread.dataset_changed", nullptr);
    broadcast_info_active_dataset_message(
        dataset.mActiveTimestamp.mSeconds,
        network_name,
        dataset.mExtendedPanId.m8,
        dataset.mMeshLocalPrefix.m8,
        dataset.mPanId,
        dataset.mChannel
    );
}

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
            break;
        }

        case OPENTHREAD_EVENT_STOP:
            orchestrator_state_set_thread_enabled(false);
            orchestrator_state_broadcast_event("thread.disabled", nullptr);
            broadcast_info_thread_stack_status_message(false);
            break;

        case OPENTHREAD_EVENT_IF_UP:
            orchestrator_state_set_thread_interface_up(true);
            broadcast_info_thread_interface_status_message(true);
            break;

        case OPENTHREAD_EVENT_IF_DOWN:
            orchestrator_state_set_thread_interface_up(false);
            broadcast_info_thread_interface_status_message(false);
            break;

        case OPENTHREAD_EVENT_ATTACHED:
            orchestrator_state_set_thread_attached(true);
            orchestrator_state_broadcast_event("thread.attached", nullptr);
            broadcast_info_thread_attachment_status_message(true);
            break;

        case OPENTHREAD_EVENT_DETACHED:
            orchestrator_state_set_thread_attached(false);
            orchestrator_state_broadcast_event("thread.detached", nullptr);
            broadcast_info_thread_attachment_status_message(false);
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

            if (active_dataset_complete(dataset_event->new_dataset)) {
                publish_active_dataset(dataset_event->new_dataset);
                break;
            }

            otOperationalDataset current_dataset = {};
            if (thread_get_active_dataset(&current_dataset) == ESP_OK &&
                active_dataset_complete(current_dataset)) {
                ESP_LOGW(TAG, "Active dataset event payload incomplete; using current active dataset");
                publish_active_dataset(current_dataset);
                break;
            }

            orchestrator_state_set_thread_dataset_present(false);
            ESP_LOGW(TAG, "Active dataset changed, but no complete active dataset is available");
            break;
        }

        default:
            ESP_LOGI(TAG, "Unhandled OpenThread event: %d", event_id);
            break;
    }
}
