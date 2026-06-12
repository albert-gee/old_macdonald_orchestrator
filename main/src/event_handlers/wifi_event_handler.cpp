#include "event_handlers/wifi_event_handler.h"

#include "messages/outbound_message_builder.h"
#include "matter_controller.h"
#include "wifi_interface.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi_types_generic.h>

#include "websocket_server.h"
#include "messages/inbound_message_handler.h"
#include "state/orchestrator_state.h"

static const char *TAG = "WIFI_EVENT_HANDLER";

void handle_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

    esp_err_t err = ESP_OK;

    if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                auto *event = static_cast<ip_event_got_ip_t *>(event_data);
                char ip[16] = {};
                esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));
                ESP_LOGI(TAG, "Wi-Fi STA got IP: %s", ip);
                orchestrator_state_set_wifi_sta_connected(true, ip);
                orchestrator_state_broadcast_event("wifi.sta_got_ip", nullptr);
                broadcast_info_wifi_status_message("got_ip");
                orchestrator_state_broadcast_snapshot();
                break;
            }
            case IP_EVENT_AP_STAIPASSIGNED: {
                auto *event = static_cast<ip_event_ap_staipassigned_t *>(event_data);
                char ip[16] = {};
                esp_ip4addr_ntoa(&event->ip, ip, sizeof(ip));
                ESP_LOGI(TAG, "Wi-Fi AP assigned station IP: %s", ip);
                matter_controller_note_ap_sta_ip(&event->ip);
                break;
            }
            default:
                ESP_LOGI(TAG, "Unhandled IP event: %d", event_id);
                break;
        }
        return;
    }

    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "Wi-Fi STA Started");
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Wi-Fi STA Connected");
            orchestrator_state_set_wifi_sta_configured(true);
            orchestrator_state_broadcast_event("wifi.sta_connected", nullptr);
            broadcast_info_wifi_status_message("connected");
            orchestrator_state_broadcast_snapshot();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "Wi-Fi STA Disconnected");
            orchestrator_state_set_wifi_sta_connected(false, nullptr);
            orchestrator_state_broadcast_event("wifi.sta_disconnected", nullptr);
            broadcast_info_wifi_status_message("disconnect");
            orchestrator_state_broadcast_snapshot();
            break;

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "Wi-Fi AP Started");
            orchestrator_state_set_wifi_ap_running(true);
            orchestrator_state_broadcast_event("wifi.ap_started", nullptr);

            // Start WebSocket server
            {
                websocket_server_handlers_t handlers = {
                    .message_handler = handle_json_inbound_message,
                    .client_event_handler = handle_websocket_client_event
                };
                err = websocket_server_start(&handlers);
            }
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WebSocket server: %s", esp_err_to_name(err));
            }
            orchestrator_state_broadcast_snapshot();

            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "Wi-Fi AP Stopped");
            orchestrator_state_set_wifi_ap_running(false);
            orchestrator_state_broadcast_event("wifi.ap_stopped", nullptr);

            // Stop WebSocket server
            err = websocket_server_stop();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to stop WebSocket server: %s", esp_err_to_name(err));
            }
            orchestrator_state_broadcast_snapshot();
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "Station connected to this AP");
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "Station disconnected from this AP");
            break;

        default:
            ESP_LOGI(TAG, "Unhandled Wi-Fi event: %d", event_id);
            break;
    }
}
