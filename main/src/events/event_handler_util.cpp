#include "events/event_handler_util.h"

#include "json/json_request_handler.h"
#include "json/json_response.h"
#include "websocket_server.h"

#include <esp_err.h>
#include <esp_log.h>

static const char *TAG = "EVENT_HANDLER_UTIL";

// Helper function to broadcast info messages
void broadcast_info_message(const char* message) {
    char* response = create_json_info_response(message);
    if (websocket_send_message_to_all_clients(response) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to broadcast: %s", message);
    }
}
