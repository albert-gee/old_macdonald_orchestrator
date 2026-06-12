#include "event_handlers/chip_event_handler.h"
#include "commands/matter_commands.h"
#include "commands/thread_cli_commands.h"
#include "event_handlers/thread_event_handler.h"
#include "event_handlers/wifi_event_handler.h"
#include "control/temperature_control.h"
#include "messages/outbound_message_builder.h"
#include "thread_interface.h"
#include "matter_interface.h"
#include "registry/device_registry.h"
#include "state/orchestrator_state.h"
#include "storage/nvs_diagnostics.h"
#include "wifi_interface.h"

#include <esp_app_desc.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <cstdio>

static const char *TAG = "ORCHESTRATOR";

extern "C" void app_main() {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "Firmware project=%s version=%s git=%s idf=%s built=%s %s",
             app_desc ? app_desc->project_name : "unknown",
             app_desc ? app_desc->version : "unknown",
             ORCHESTRATOR_GIT_COMMIT,
             app_desc ? app_desc->idf_ver : "unknown",
             app_desc ? app_desc->date : "unknown",
             app_desc ? app_desc->time : "unknown");

    ESP_LOGI(TAG, "Initializing NVS partitions");
    ESP_ERROR_CHECK(init_nvs_partition_or_recover(nullptr));
    ESP_ERROR_CHECK(init_nvs_partition_or_recover(OT_NVS_PARTITION_NAME));
    ESP_ERROR_CHECK(init_nvs_partition_or_recover("matter_nvs"));
    log_nvs_stats(nullptr);
    log_nvs_stats(OT_NVS_PARTITION_NAME);
    log_nvs_stats("matter_nvs");

    ESP_LOGI(TAG, "Initializing Orchestrator state");
    ESP_ERROR_CHECK(orchestrator_state_init());

    ESP_LOGI(TAG, "Initializing outbound message workers");
    ESP_ERROR_CHECK(outbound_message_builder_init());

    ESP_LOGI(TAG, "Initializing device registry");
    ESP_ERROR_CHECK(device_registry_init());

    ESP_LOGI(TAG, "Initializing temperature control");
    ESP_ERROR_CHECK(temperature_control_init());

    ESP_LOGI(TAG, "Initializing Matter command worker");
    ESP_ERROR_CHECK(matter_command_service_init());

    // Create the default event loop
    ESP_LOGI(TAG, "Creating default event loop");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
        return;
    }

    // Initialize Netif
    ESP_LOGI(TAG, "Initializing esp_netif");
    err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize esp_netif: %s", esp_err_to_name(err));
        return;
    }

    // Initialize Wi-Fi Interface
#if CONFIG_ENABLE_WIFI_STATION || CONFIG_ENABLE_WIFI_AP
    err = wifi_interface_init(handle_wifi_event);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi interface: %s", esp_err_to_name(err));
        return;
    }
#endif

    // Initialize Thread Interface
#if CONFIG_OPENTHREAD_ENABLED
    err = thread_interface_init(handle_thread_event);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Thread stack: %s", esp_err_to_name(err));
        orchestrator_state_set_thread_platform_initialized(false);
    } else {
        orchestrator_state_set_thread_platform_initialized(true);
        err = thread_cli_service_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize Thread CLI service: %s", esp_err_to_name(err));
        }
    }
#endif // CONFIG_OPENTHREAD_ENABLED

    // Initialize Matter Interface
    err = matter_interface_init(handle_chip_device_event, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Matter interface: %s", esp_err_to_name(err));
        char platform_error[192] = {};
        const char *matter_detail = matter_interface_get_last_error();
        if (matter_detail && matter_detail[0] != '\0') {
            snprintf(platform_error, sizeof(platform_error), "MATTER_PLATFORM_INIT_FAILED:%s", matter_detail);
        } else {
            snprintf(platform_error, sizeof(platform_error), "MATTER_PLATFORM_INIT_FAILED:%s", esp_err_to_name(err));
        }
        orchestrator_state_set_matter_platform_error(platform_error);
    } else {
        orchestrator_state_set_matter_platform_initialized(true);
    }

    // Start Wi-Fi
    // This step should go after the Matter stack initialization because Matter disables Wi-Fi AP mode
    err = wifi_interface_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi AP+STA: %s", esp_err_to_name(err));
    }
}
