#include "event_handlers/wifi_event_handler.h"

#include "messages/json_inbound_message.h"
#include "messages/json_outbound_message.h"
#include "wifi_interface.h"
#include "thread_util.h"
#include "websocket_server.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_openthread_border_router.h>
#include <esp_wifi_types_generic.h>

static const char *TAG = "WIFI_EVENT_HANDLER";

#if CONFIG_OPENTHREAD_BORDER_ROUTER
static bool sThreadBRInitialized = false;
#endif

void handle_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    esp_err_t err = ESP_OK;

    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "Wi-Fi STA Started");
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Wi-Fi STA Connected");
            create_info_wifi_status_message("connected");

#if CONFIG_OPENTHREAD_BORDER_ROUTER
            if (!sThreadBRInitialized) {
                ESP_LOGI(TAG, "Starting OpenThread Border Router");
                err = thread_br_init();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to initialize OpenThread BR: %s", esp_err_to_name(err));
                } else {
                    ESP_LOGI(TAG, "OpenThread Border Router initialized");
                    sThreadBRInitialized = true;
                }
            }
#endif
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "Wi-Fi STA Disconnected");
            create_info_wifi_status_message("disconnect");
            break;

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "Wi-Fi AP Started");
            err = websocket_server_start(handle_json_inbound_message);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WebSocket server: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "WebSocket server started");
            }
            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "Wi-Fi AP Stopped");
            err = websocket_server_stop();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to stop WebSocket server: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "WebSocket server stopped");
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
