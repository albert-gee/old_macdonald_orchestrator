#include "matter_interface.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_core.h>
#include <esp_matter_ota.h>
#include <esp_matter_providers.h>

static const char *TAG = "MATTER_INTERFACE";

static bool esp_matter_started = false;

esp_err_t matter_interface_init(const esp_matter::event_callback_t handle_chip_device_event,
                                const intptr_t callback_arg) {
    ESP_LOGI(TAG, "Initializing Matter interface");
    if (esp_matter_started) {
        ESP_LOGE(TAG, "Matter stack already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize OTA requestor (required before Matter stack start)
    esp_matter_ota_requestor_init();

    // Initialize CHIP memory
    if (chip::Platform::MemoryInit() != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "CHIP memory initialization failed");
        return ESP_ERR_NO_MEM;
    }

    // Initialize CHIP stack. This step changes Wi-Fi mode to STA
    ESP_LOGI(TAG, "Initializing CHIP stack");
    if (chip::DeviceLayer::PlatformMgr().InitChipStack() != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "CHIP stack initialization failed");
        return ESP_FAIL;
    }

    // Set up default Matter providers (e.g., device info, configuration)
    esp_matter::setup_providers();

    // Start Matter platform event loop
    if (chip::DeviceLayer::PlatformMgr().StartEventLoopTask() != CHIP_NO_ERROR) {
        chip::Platform::MemoryShutdown();
        ESP_LOGE(TAG, "Failed to start Matter event loop");
        return ESP_FAIL;
    }

    // Register the event handler
    ESP_LOGI(TAG, "Registering internal and optional external event handlers");
    chip::DeviceLayer::PlatformMgr().AddEventHandler(handle_chip_device_event, 0);

    esp_matter_started = true;

    return ESP_OK;
}
