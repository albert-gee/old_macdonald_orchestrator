#include "events/chip_event_handler.h"
#include "events/thread_event_handler.h"
#include "events/wifi_event_handler.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_matter.h>
#include <esp_matter_ota.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_console.h>
#include <esp_ot_config.h>

#include <platform/ESP32/OpenthreadLauncher.h>

#include <portmacro.h>

static const char *TAG = "ORCHESTRATOR";

#define MATTER_CONTROLLER_NODE_ID 1234
#define MATTER_CONTROLLER_FABRIC_ID 1
#define MATTER_CONTROLLER_LISTEN_PORT 5580

extern "C" void app_main() {
    // Initialize ESP NVS layer
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
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = ESP_OPENTHREAD_DEFAULT_CONFIG();
    set_openthread_platform_config(&config);
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER


    // ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    //
    // // Register the event handler
    // esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, handle_wifi_event, nullptr);
    // esp_event_handler_register(OPENTHREAD_EVENT, ESP_EVENT_ANY_ID, handle_thread_event, nullptr);
    //
    //
    // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    //
    // esp_netif_create_default_wifi_sta();
    //
    // wifi_config_t wifi_sta_config = {
    //     .sta = {
    //         .ssid = "SkyNet_Guest",
    //         .password = "password147",
    //         .threshold = {
    //             .authmode = WIFI_AUTH_WPA2_PSK,
    //         },
    //     }
    // };
    // ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));
    //
    // ESP_ERROR_CHECK(esp_wifi_start());
    //
    // ESP_LOGI(TAG, "wifi_init finished.");


    // Start the Matter stack
    err = esp_matter::start(handle_chip_device_event);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter stack");
        return;
    }
    ESP_LOGI(TAG, "Matter stack started successfully");

    //
    // Register the event handler
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, handle_wifi_event, nullptr);
    esp_event_handler_register(OPENTHREAD_EVENT, ESP_EVENT_ANY_ID, handle_thread_event, nullptr);


    // Matter CLI commands
#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
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
