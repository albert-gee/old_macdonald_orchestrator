#include "events/chip_event_handler.h"
#include "events/thread_event_handler.h"
#include "events/wifi_event_handler.h"
#include "thread_interface.h"
#include "matter_interface.h"
#include "wifi_interface.h"

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
// #include <esp_wifi_types_generic.h>

#include <esp_netif.h>
#include <esp_openthread_types.h>
#include <esp_vfs_eventfd.h>


#define MATTER_CONTROLLER_NODE_ID 1234
#define MATTER_CONTROLLER_FABRIC_ID 1
#define MATTER_CONTROLLER_LISTEN_PORT 5580

static const char *TAG = "ORCHESTRATOR";

extern "C" void app_main() {
    // Initialize ESP NVS layer
    ESP_LOGI(TAG, "Initializing NVS Flash");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Create default event loop
    ESP_LOGI(TAG, "Creating default event loop");
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
        return;
    }

    // Register the eventfd for the OpenThread stack
    // Used event fds: netif, ot task queue, radio driver
    ESP_LOGI(TAG, "Registering eventfd for OpenThread stack");
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };
    err = esp_vfs_eventfd_register(&eventfd_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register OpenThread eventfd");
        return;
    }

    // Initialize Netif
    ESP_LOGI(TAG, "Initializing esp_netif");
    err = esp_netif_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize esp_netif: %s", esp_err_to_name(err));
        return;
    }

    // Initialize Thread Interface
#if CONFIG_OPENTHREAD_ENABLED
    esp_event_handler_register(OPENTHREAD_EVENT, ESP_EVENT_ANY_ID, handle_thread_event, nullptr);

    err = thread_interface_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize Thread stack: %s", esp_err_to_name(err));
        esp_vfs_eventfd_unregister();
        return;
    }
#endif // CONFIG_OPENTHREAD_ENABLED

    // Initialize Wi-Fi Interface
#if CONFIG_ENABLE_WIFI_STATION
    ESP_LOGI(TAG, "Initializing Wi-Fi interface");
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, handle_wifi_event, nullptr);
    wifi_interface_init();
//     VerifyOrReturnError(chip::DeviceLayer::Internal::ESP32Utils::InitWiFiStack() == CHIP_NO_ERROR, ESP_FAIL, ESP_LOGE(TAG, "Error initializing Wi-Fi stack"));
#endif

    // Initialize Matter Interface and Controller
    ESP_LOGI(TAG, "Initializing Matter interface");
    matter_interface_init(handle_chip_device_event, 0);
    matter_interface_controller_init(MATTER_CONTROLLER_NODE_ID, MATTER_CONTROLLER_FABRIC_ID, MATTER_CONTROLLER_LISTEN_PORT);



    ESP_LOGI(TAG, "Starting Wi-Fi AP+STA");
    start_wifi_ap_sta();

}
