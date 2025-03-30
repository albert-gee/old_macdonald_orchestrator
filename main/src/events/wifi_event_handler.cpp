#include "events/wifi_event_handler.h"
#include "wifi_interface.h"
#include "json/json_request_handler.h"

#include <cstdint>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi_types_generic.h>
#include <websocket_server.h>

static const char *TAG = "WIFI_EVENT_HANDLER";

void handle_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    // Wi-Fi events
    ESP_LOGI(TAG, "WiFi EVENT: %d", event_id);

    // Wi-Fi STA started.
    if (event_id == WIFI_EVENT_STA_START) {

        // Start Wi-Fi Access Point
        esp_err_t err = start_wifi_ap("SkyNet_Guest", "password147");
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to connect to Wi-Fi, err:%s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Connecting to Wi-Fi...");
        }

    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        // Wi-Fi STA connected. Start WebSocket server

        ESP_LOGI(TAG, "Wi-Fi STA Connected. Starting WebSocket server...");


    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Wi-Fi STA disconnected. Stop WebSocket server

        ESP_LOGI(TAG, "Wi-Fi Disconnected");
    } else if (event_id == WIFI_EVENT_AP_START) {
        // Wi-Fi AP started. Start WebSocket server

        esp_err_t err = websocket_server_start(handle_json_request);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start WebSocket server, err:%d", err);
        }
        ESP_LOGI(TAG, "WebSocket server started");

        // Connect to Wi-Fi STA
        err = connect_to_wifi("SkyNet_Guest", "password147");
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to connect to Wi-Fi, err:%s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Connecting to Wi-Fi...");
        }

    } else if (event_id == WIFI_EVENT_AP_STOP) {
        // Wi-Fi AP stopped. Stop WebSocket server

        ESP_LOGI(TAG, "Wi-Fi AP Stopped");
        esp_err_t err = websocket_server_stop();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop WebSocket server, err:%d", err);
        }
    } else {
        ESP_LOGI(TAG, "Unhandled Wi-Fi event: %d", event_id);
    }
}
