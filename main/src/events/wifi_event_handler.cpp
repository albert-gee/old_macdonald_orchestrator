#include "events/wifi_event_handler.h"
#include "json/json_request_handler.h"
#include "utils/event_handler_util.h"
#include "wifi_interface.h"
#include "websocket_server.h"
#include "thread_util.h"

#include <cstdint>
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
        // OpenThread started
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "Wi-Fi STA Started");
            break;

        case WIFI_EVENT_STA_CONNECTED:
            // Wi-Fi STA connected.
            ESP_LOGI(TAG, "Wi-Fi STA Connected.");
            broadcast_wifi_sta_status("connected");
#if CONFIG_OPENTHREAD_BORDER_ROUTER
            if (!sThreadBRInitialized) {
                ESP_LOGI(TAG, "Starting OpenThread Border Router");
                err = thread_br_init();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to initialize OpenThread Border Router, err:%s", esp_err_to_name(err));
                    broadcast_info_message("Failed to initialize OpenThread Border Router");
                } else {
                    ESP_LOGI(TAG, "OpenThread Border Router initialized successfully");
                    sThreadBRInitialized = true;
                    broadcast_info_message("OpenThread Border Router initialized successfully");
                }
            }
#endif
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            // Wi-Fi STA disconnected.
            ESP_LOGI(TAG, "Wi-Fi Disconnected");
            broadcast_wifi_sta_status("disconnect");
            break;

        case WIFI_EVENT_AP_START:
            // Wi-Fi AP started.
            ESP_LOGI(TAG, "Wi-Fi AP Started");

            // Start WebSocket server
            err = websocket_server_start(handle_json_request);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WebSocket server, err:%s", esp_err_to_name(err));
            }
            ESP_LOGI(TAG, "WebSocket server started");
            break;

        case WIFI_EVENT_AP_STOP:
            // Wi-Fi AP stopped.
            ESP_LOGI(TAG, "Wi-Fi AP Stopped");

            // Stop WebSocket server
            err = websocket_server_stop();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to stop WebSocket server, err:%s", esp_err_to_name(err));
            }
            ESP_LOGI(TAG, "WebSocket server stopped");
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            // A station connected to the AP.
            ESP_LOGI(TAG, "A station connected to the AP");
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "A station disconnected from the AP");
            break;

        default:
            ESP_LOGI(TAG, "WiFi EVENT: %d", event_id);
            break;
    }
}
