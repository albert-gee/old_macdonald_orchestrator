#include "event_handler.h"
#include "json_request_handler.h"
#include "websocket_server.h"

#include <esp_matter.h>
#include <esp_err.h>
#include <esp_openthread_types.h>
#include <esp_wifi.h>
#include <json_response.h>
#include <thread_util.h>

#if CONFIG_OPENTHREAD_BORDER_ROUTER
#include <esp_openthread_border_router.h>
#include <esp_openthread_lock.h>
#include <esp_spiffs.h>
// #include <platform/ESP32/OpenthreadLauncher.h>
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER

static const char *TAG = "CHIP_EVENT_HANDLER";

#if CONFIG_OPENTHREAD_BORDER_ROUTER
static bool sThreadBRInitialized = false;
#endif

// Helper function to broadcast info messages
static void broadcast_info_message(const char* message) {
    char* response = create_json_info_response(message);
    if (websocket_send_message_to_all_clients(response) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to broadcast: %s", message);
    }
}



void handle_chip_device_event(const ChipDeviceEvent *event, intptr_t arg) {

    switch (event->Type) {
        case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInterfaceIpAddressChanged:
            ESP_LOGI(TAG, "Interface IP Address changed");
            break;

        case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:

            if (event->Platform.ESPSystemEvent.Base == WIFI_EVENT) {
            } else if (event->Platform.ESPSystemEvent.Base == IP_EVENT &&
                       event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP) {
                // Wi-Fi STA got IP address. Start Thread Border Router

                ESP_LOGI(TAG, "IP Address assigned to Wi-Fi interface");

                #if CONFIG_OPENTHREAD_BORDER_ROUTER
                    if (!sThreadBRInitialized) {
                        ESP_LOGI(TAG, "Starting OpenThread Border Router");
                        broadcast_info_message("Starting OpenThread Border Router");
                        // Initialize OpenThread Border Router
                        esp_openthread_set_backbone_netif(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));
                        esp_openthread_lock_acquire(portMAX_DELAY);
                        esp_openthread_border_router_init();
                        esp_openthread_lock_release();
                        sThreadBRInitialized = true;
                    }
                #endif
            }
            break;
        default:
            break;
    }
}


void handle_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    // Wi-Fi events
    ESP_LOGI(TAG, "WiFi EVENT: %d", event_id);

    // Wi-Fi STA started. Connect to AP
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi Started");

        wifi_config_t wifi_cfg = {};
        snprintf((char *) wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", "SkyNet_Guest");
        snprintf((char *) wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", "password147");
        wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        wifi_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set Wi-Fi configuration: %s", esp_err_to_name(ret));
        }

        ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGI(TAG, "Wi-Fi Connection Failed");
            if (ret == ESP_ERR_WIFI_NOT_INIT) {
                ESP_LOGE(TAG, "Wi-Fi not initialized. Call esp_wifi_init() first.");
            } else if (ret == ESP_ERR_WIFI_NOT_STARTED) {
                ESP_LOGE(TAG, "Wi-Fi not started. Call esp_wifi_start() before connecting.");
            } else if (ret == ESP_ERR_WIFI_MODE) {
                ESP_LOGE(TAG, "Wi-Fi mode error. Ensure Wi-Fi is in station mode (WIFI_MODE_STA).");
            } else if (ret == ESP_ERR_WIFI_CONN) {
                ESP_LOGE(TAG, "Wi-Fi connection failed due to internal error.");
            } else if (ret == ESP_ERR_WIFI_SSID) {
                ESP_LOGE(TAG, "Invalid SSID. Ensure the SSID is correctly set before connecting.");
            } else {
                ESP_LOGE(TAG, "Wi-Fi connection failed with unknown error: 0x%x", ret);
            }
        }
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        // Wi-Fi STA connected. Start WebSocket server

        ESP_LOGI(TAG, "Wi-Fi STA Connected. Starting WebSocket server...");
        esp_err_t err = websocket_server_start(handle_request);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start WebSocket server, err:%d", err);
        }
        ESP_LOGI(TAG, "WebSocket server started");
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Wi-Fi STA disconnected. Stop WebSocket server

        ESP_LOGI(TAG, "Wi-Fi Disconnected");
        esp_err_t err = websocket_server_stop();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop WebSocket server, err:%d", err);
        }
    }
}

void handle_thread_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "OpenThread EVENT: %d", event_id);

    esp_err_t err;

    otOperationalDataset dataset;

    switch (event_id) {
        // Dataset Changed
        case OPENTHREAD_EVENT_DATASET_CHANGED:
            ESP_LOGI(TAG, "OpenThread dataset changed");

            // Acquire OpenThread lock
            esp_openthread_lock_acquire(portMAX_DELAY);

            err = thread_get_active_dataset(&dataset);
            if (err == ESP_OK) {
                char mesh_local_prefix_hex[2 * OT_MESH_LOCAL_PREFIX_SIZE + 1] = {0};
                // 2 characters per byte + null terminator
                binary_to_hex_string(dataset.mMeshLocalPrefix.m8, OT_MESH_LOCAL_PREFIX_SIZE, mesh_local_prefix_hex,
                                     sizeof(mesh_local_prefix_hex));

                const char *response = create_json_set_dataset_response(
                    dataset.mActiveTimestamp.mSeconds, // Pass mSeconds (or another field if appropriate)
                    dataset.mPendingTimestamp.mSeconds, // Pass mSeconds (or another field if appropriate)
                    dataset.mNetworkKey.m8, // Network Key
                    dataset.mNetworkName.m8, // Network Name
                    dataset.mExtendedPanId.m8, // Extended PAN ID
                    mesh_local_prefix_hex, // Mesh Local Prefix
                    dataset.mDelay, // Delay Timer
                    dataset.mPanId, // PAN ID
                    dataset.mChannel, // Channel
                    dataset.mWakeupChannel, // Wake-up Channel
                    dataset.mPskc.m8 // PSKc
                );

                broadcast_info_message(response);

            } else {
                ESP_LOGE(TAG, "Failed to get device role name");
            }

            esp_openthread_lock_release();

            break;

        // Role Changed
        case OPENTHREAD_EVENT_ROLE_CHANGED:
            ESP_LOGI(TAG, "OpenThread role changed");

            char device_role_name[32];
            err = thread_get_device_role_name(device_role_name);
            if (err == ESP_OK) {
                char message[64];
                sprintf(message, "Device role changed to: %s", device_role_name);
                broadcast_info_message(message);
            } else {
                ESP_LOGE(TAG, "Failed to get device role name");
            }
            break;

        case OPENTHREAD_EVENT_START:
            ESP_LOGI(TAG, "OpenThread stack started");
            broadcast_info_message("OpenThread stack started");
            break;

        case OPENTHREAD_EVENT_STOP:
            ESP_LOGI(TAG, "OpenThread stack stopped");
            broadcast_info_message("OpenThread stack stopped");
            break;

        case OPENTHREAD_EVENT_DETACHED:
            ESP_LOGI(TAG, "OpenThread detached");
            broadcast_info_message("OpenThread detached");
            break;

        case OPENTHREAD_EVENT_ATTACHED:
            ESP_LOGI(TAG, "OpenThread attached");
            broadcast_info_message("OpenThread attached");
            break;

        case OPENTHREAD_EVENT_IF_UP:
            ESP_LOGI(TAG, "OpenThread network interface up");
            broadcast_info_message("OpenThread network interface up");
            break;

        case OPENTHREAD_EVENT_IF_DOWN:
            ESP_LOGI(TAG, "OpenThread network interface down");
            broadcast_info_message("OpenThread network interface down");
            break;

        case OPENTHREAD_EVENT_GOT_IP6:
            ESP_LOGI(TAG, "OpenThread stack added IPv6 address");
            broadcast_info_message("OpenThread stack added IPv6 address");
            break;

        case OPENTHREAD_EVENT_LOST_IP6:
            ESP_LOGI(TAG, "OpenThread stack removed IPv6 address");
            broadcast_info_message("OpenThread stack removed IPv6 address");
            break;

        case OPENTHREAD_EVENT_MULTICAST_GROUP_JOIN:
            ESP_LOGI(TAG, "OpenThread stack joined IPv6 multicast group");
            broadcast_info_message("OpenThread stack joined IPv6 multicast group");
            break;

        case OPENTHREAD_EVENT_MULTICAST_GROUP_LEAVE:
            ESP_LOGI(TAG, "OpenThread stack left IPv6 multicast group");
            broadcast_info_message("OpenThread stack left IPv6 multicast group");
            break;

        case OPENTHREAD_EVENT_TREL_ADD_IP6:
            ESP_LOGI(TAG, "OpenThread stack added TREL IPv6 address");
            broadcast_info_message("OpenThread stack added TREL IPv6 address");
            break;

        case OPENTHREAD_EVENT_TREL_REMOVE_IP6:
            ESP_LOGI(TAG, "OpenThread stack removed TREL IPv6 address");
            broadcast_info_message("OpenThread stack removed TREL IPv6 address");
            break;

        case OPENTHREAD_EVENT_TREL_MULTICAST_GROUP_JOIN:
            ESP_LOGI(TAG, "OpenThread stack joined TREL IPv6 multicast group");
            broadcast_info_message("OpenThread stack joined TREL IPv6 multicast group");
            break;

        case OPENTHREAD_EVENT_SET_DNS_SERVER:
            ESP_LOGI(TAG, "OpenThread stack set DNS server");
            broadcast_info_message("OpenThread stack set DNS server");
            break;

        case OPENTHREAD_EVENT_PUBLISH_MESHCOP_E:
            ESP_LOGI(TAG, "OpenThread stack started publishing meshcop-e service");
            broadcast_info_message("OpenThread stack started publishing meshcop-e service");
            break;

        case OPENTHREAD_EVENT_REMOVE_MESHCOP_E:
            ESP_LOGI(TAG, "OpenThread stack removed meshcop-e service");
            broadcast_info_message("OpenThread stack removed meshcop-e service");
            break;

        default:
            ESP_LOGW(TAG, "Unknown OpenThread event: %d", event_id);
            break;

    }
}
