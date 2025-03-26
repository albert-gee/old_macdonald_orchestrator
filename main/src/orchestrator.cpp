#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_matter.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_ota.h>

#include <esp_matter_console.h>
#include <esp_matter_controller_console.h>

#include <esp_ot_config.h>
#include "json_request_handler.h"

#if CONFIG_OPENTHREAD_BORDER_ROUTER
#include <esp_openthread_border_router.h>
#include <esp_openthread_lock.h>
#include <esp_spiffs.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER

#include <esp_wifi.h>
#include <portmacro.h>
#include <websocket_server.h>

static const char *TAG = "MAIN";

#define MATTER_CONTROLLER_NODE_ID 1234
#define MATTER_CONTROLLER_FABRIC_ID 1
#define MATTER_CONTROLLER_LISTEN_PORT 5580

#if CONFIG_OPENTHREAD_BORDER_ROUTER
static bool sThreadBRInitialized = false;
#endif

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg) {
    switch (event->Type) {
        case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInterfaceIpAddressChanged:
            ESP_LOGI(TAG, "Interface IP Address changed");
            break;

        case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:
            if (event->Platform.ESPSystemEvent.Base == WIFI_EVENT) {

                // Wi-Fi STA started. Connect to AP
                if (event->Platform.ESPSystemEvent.Id == WIFI_EVENT_STA_START) {
                    ESP_LOGI(TAG, "Wi-Fi Started: connecting...");

                    esp_err_t ret;

                    wifi_config_t wifi_cfg = {};
                    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", "SkyNet_Guest");
                    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", "password147");
                    wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
                    wifi_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

                    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to set Wi-Fi configuration: %s", esp_err_to_name(ret));
                    }

                    ret = esp_wifi_connect();

                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "Wi-Fi Connected");
                    } else {
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
                } else if (event->Platform.ESPSystemEvent.Id == WIFI_EVENT_STA_CONNECTED) {
                    // Wi-Fi STA connected. Start WebSocket server

                    ESP_LOGI(TAG, "Wi-Fi STA Connected. Starting WebSocket server...");
                    esp_err_t err = websocket_server_start(handle_request);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to start WebSocket server, err:%d", err);
                    }
                    ESP_LOGI(TAG, "WebSocket server started");

                } else if (event->Platform.ESPSystemEvent.Id == WIFI_EVENT_STA_DISCONNECTED) {
                    // Wi-Fi STA disconnected. Stop WebSocket server

                    ESP_LOGI(TAG, "Wi-Fi Disconnected");
                    esp_err_t err = websocket_server_stop();
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to stop WebSocket server, err:%d", err);
                    }
                }

            } else if (event->Platform.ESPSystemEvent.Base == IP_EVENT &&
                event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP) {
                // Wi-Fi STA got IP address. Start Thread Border Router

                ESP_LOGI(TAG, "IP Address assigned to Wi-Fi interface");

#if CONFIG_OPENTHREAD_BORDER_ROUTER
                if (!sThreadBRInitialized) {
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

extern "C" void app_main() {
    // Initialize the ESP NVS layer
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Matter CLI commands
#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();

    // Matter Controller CLI commands
#if CONFIG_ESP_MATTER_CONTROLLER_ENABLE
    esp_matter::console::controller_register_commands();
#endif

    // OpenThread CLI commands
#ifdef CONFIG_OPENTHREAD_BORDER_ROUTER
    esp_matter::console::otcli_register_commands();
#endif

#endif

    // Configure OpenThread Border Router
#ifdef CONFIG_OPENTHREAD_BORDER_ROUTER
#ifdef CONFIG_AUTO_UPDATE_RCP
        esp_vfs_spiffs_conf_t rcp_fw_conf = {
            .base_path = "/rcp_fw", .partition_label = "rcp_fw", .max_files = 10, .format_if_mount_failed = false};
        if (ESP_OK != esp_vfs_spiffs_register(&rcp_fw_conf)) {
            ESP_LOGE(TAG, "Failed to mount rcp firmware storage");
            return;
        }
        esp_rcp_update_config_t rcp_update_config = ESP_OPENTHREAD_RCP_UPDATE_CONFIG();
        openthread_init_br_rcp(&rcp_update_config);
#endif
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = ESP_OPENTHREAD_DEFAULT_CONFIG();
    set_openthread_platform_config(&config);
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER

    // Start the Matter stack
    err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter stack");
        return;
    }
    ESP_LOGI(TAG, "Matter stack started successfully");

    // Locking the Matter thread
    esp_matter::lock::chip_stack_lock(portMAX_DELAY);

    // Initialize the Matter controller client
    err = esp_matter::controller::matter_controller_client::get_instance().init(MATTER_CONTROLLER_NODE_ID, MATTER_CONTROLLER_FABRIC_ID, MATTER_CONTROLLER_LISTEN_PORT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Matter controller client");
        return;
    }
    ESP_LOGI(TAG, "Matter controller client initialized successfully");

    // Set up commissioner
#ifdef CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
    err = esp_matter::controller::matter_controller_client::get_instance().setup_commissioner();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup commissioner");
        return;
    }
    ESP_LOGI(TAG, "Commissioner setup successfully");
#endif

    // Unlock the Matter thread
    esp_matter::lock::chip_stack_unlock();
}
