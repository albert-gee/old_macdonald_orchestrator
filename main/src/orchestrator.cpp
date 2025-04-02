#include "events/chip_event_handler.h"
#include "events/thread_event_handler.h"
#include "events/wifi_event_handler.h"
#include <esp_ot_config.h>

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_matter.h>
#include <esp_matter_ota.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_console.h>

#include <platform/ESP32/OpenthreadLauncher.h>

#include <portmacro.h>

static const char *TAG = "ORCHESTRATOR";

#define MATTER_CONTROLLER_NODE_ID 1234
#define MATTER_CONTROLLER_FABRIC_ID 1
#define MATTER_CONTROLLER_LISTEN_PORT 5580

extern "C" void app_main() {
    // Initialize ESP NVS layer
    ESP_LOGI(TAG, "Initializing NVS Flash");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Configure OpenThread Border Router
#ifdef CONFIG_OPENTHREAD_BORDER_ROUTER
#ifdef CONFIG_AUTO_UPDATE_RCP
    ESP_LOGI(TAG, "Registering RCP firmware storage and initializing OpenThread RCP");
    esp_vfs_spiffs_conf_t rcp_fw_conf = {
        .base_path = "/rcp_fw", .partition_label = "rcp_fw", .max_files = 10, .format_if_mount_failed = false};
    if (ESP_OK != esp_vfs_spiffs_register(&rcp_fw_conf)) {
        ESP_LOGE(TAG, "Failed to mount rcp firmware storage");
        return;
    }
    esp_rcp_update_config_t rcp_update_config = ESP_OPENTHREAD_RCP_UPDATE_CONFIG();
    openthread_init_br_rcp(&rcp_update_config);
#endif
    ESP_LOGI(TAG, "Setting OpenThread Default Configuration");
    esp_openthread_platform_config_t config = ESP_OPENTHREAD_DEFAULT_CONFIG();
    set_openthread_platform_config(&config);
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER

    // Create default event loop
    ESP_LOGI(TAG, "Creating default event loop");
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create default event loop");
        return;
    }

    // Register OpenThread and Wi-Fi event handlers
    ESP_LOGI(TAG, "Registering event handlers");
    esp_event_handler_register(OPENTHREAD_EVENT, ESP_EVENT_ANY_ID, handle_thread_event, nullptr);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, handle_wifi_event, nullptr);

    // Start Matter stack
    // This function also initializes Wi-Fi stack and OpenThread stack
    ESP_LOGI(TAG, "Starting Matter stack");
    err = esp_matter::start(handle_chip_device_event);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter stack");
        return;
    }

    // Initialize Matter CLI
#if CONFIG_ENABLE_CHIP_SHELL
    ESP_LOGI(TAG, "Registering Matter CLI commands");

    // Diagnostics, Wi-Fi, and Factory Reset CLI commands
    err = esp_matter::console::diagnostics_register_commands();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register diagnostics commands");
        return;
    }
    err = esp_matter::console::wifi_register_commands();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register Wi-Fi commands");
        return;
    }
    err = esp_matter::console::factoryreset_register_commands();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register factory reset commands");
        return;
    }

    // Initialize Matter console
    ESP_LOGI(TAG, "Initializing Matter console");
    err = esp_matter::console::init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Matter console");
        return;
    }

    // Matter Controller CLI commands
#if CONFIG_ESP_MATTER_CONTROLLER_ENABLE
    ESP_LOGI(TAG, "Registering Matter Controller CLI commands");
    esp_matter::console::controller_register_commands();
#endif

    // OpenThread CLI commands
#ifdef CONFIG_OPENTHREAD_BORDER_ROUTER
    ESP_LOGI(TAG, "Registering OpenThread CLI commands");
    esp_matter::console::otcli_register_commands();
#endif

#endif

    // Initialize Matter Controller Client and setup commissioner
    ESP_LOGI(TAG, "Initializing Matter Controller Client");
    // Locking the Matter thread
    esp_matter::lock::chip_stack_lock(portMAX_DELAY);

    // Initialize the Matter controller client
    err = esp_matter::controller::matter_controller_client::get_instance().init(
        MATTER_CONTROLLER_NODE_ID, MATTER_CONTROLLER_FABRIC_ID, MATTER_CONTROLLER_LISTEN_PORT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Matter controller client");
        return;
    }
    ESP_LOGI(TAG, "Matter controller client initialized successfully");

    // Set up commissioner
#ifdef CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
    ESP_LOGI(TAG, "Setting up commissioner");
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
