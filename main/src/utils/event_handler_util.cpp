#include "utils/event_handler_util.h"

#include "json/json_request_handler.h"
#include "json/json_response.h"
#include "websocket_server.h"

#include <esp_err.h>
#include <esp_log.h>

static const char *TAG = "EVENT_HANDLER_UTIL";

// Helper function to broadcast info messages
void broadcast_info_message(const char *message) {
    char *response = create_json_info_response(message);
    if (websocket_send_message_to_all_clients(response) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to broadcast: %s", message);
    }
}

void broadcast_error_message(const char *message) {
    char *response = create_json_error_response(message);
    if (websocket_send_message_to_all_clients(response) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to broadcast: %s", message);
    }
}

void broadcast_thread_dataset_active_message(uint64_t active_timestamp, uint64_t pending_timestamp,
                                     const uint8_t *network_key, const char *network_name,
                                     const uint8_t *extended_pan_id, const char *mesh_local_prefix,
                                     uint32_t delay, uint16_t pan_id, uint16_t channel,
                                     uint16_t wakeup_channel, const uint8_t *pskc) {
    const char *response = create_json_set_dataset_response(
        active_timestamp, // Pass mSeconds (or another field if appropriate)
        pending_timestamp, // Pass mSeconds (or another field if appropriate)
        network_key, // Network Key
        network_name, // Network Name
        extended_pan_id, // Extended PAN ID
        mesh_local_prefix, // Mesh Local Prefix
        delay, // Delay Timer
        pan_id, // PAN ID
        channel, // Channel
        wakeup_channel, // Wake-up Channel
        pskc // PSKc
    );
    if (websocket_send_message_to_all_clients(response) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to broadcast thread_dataset_active");
    }
}

void broadcast_thread_role_set_message(const char *role) {
    char *response = create_json_thread_role_set_response(role);
    if (websocket_send_message_to_all_clients(response) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to broadcast thread_role_set: %s", role);
    }
}