#include "event_handlers/wifi_event_handler.h"

#include "messages/outbound_message_builder.h"
#include "wifi_interface.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi_types_generic.h>

#include "websocket_server.h"
#include "messages/inbound_message_handler.h"

static const char *TAG = "WIFI_EVENT_HANDLER";

void handle_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

    esp_err_t err = ESP_OK;

    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "Wi-Fi STA Started");
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Wi-Fi STA Connected");
            broadcast_info_wifi_status_message("connected");
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "Wi-Fi STA Disconnected");
            broadcast_info_wifi_status_message("disconnect");
            break;

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "Wi-Fi AP Started");

            // Start WebSocket server
            err = websocket_server_start(send_websocket_connected_message_to_client, handle_json_inbound_message);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WebSocket server: %s", esp_err_to_name(err));
            }

            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "Wi-Fi AP Stopped");

            // Stop WebSocket server
            err = websocket_server_stop();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to stop WebSocket server: %s", esp_err_to_name(err));
            }
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "Station connected to AP");
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "Station disconnected from AP");
            break;

        default:
            ESP_LOGI(TAG, "Unhandled Wi-Fi event: %d", event_id);
            break;
    }
}
