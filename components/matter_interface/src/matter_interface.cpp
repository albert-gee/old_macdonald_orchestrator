#include "matter_interface.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_console.h>
#include <esp_matter_core.h>
#include <esp_matter_ota.h>
#include <esp_matter_providers.h>
#include <app/server/Dnssd.h>

static const char *TAG = "MATTER_INTERFACE";

static bool esp_matter_started = false;

static void device_callback_internal(const ChipDeviceEvent *event, intptr_t arg) {
    switch (event->Type) {
        case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI || CHIP_DEVICE_CONFIG_ENABLE_ETHERNET
            if (event->InterfaceIpAddressChanged.Type == chip::DeviceLayer::InterfaceIpChangeType::kIpV6_Assigned ||
                event->InterfaceIpAddressChanged.Type == chip::DeviceLayer::InterfaceIpChangeType::kIpV4_Assigned) {
                chip::app::DnssdServer::Instance().StartServer();
            }
#endif
            break;
#ifdef CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER
        case chip::DeviceLayer::DeviceEventType::kDnssdInitialized:
            esp_matter_ota_requestor_start();
        /* Initialize binding manager */
        client::binding_manager_init();
        break;

        case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
            ESP_LOGI(TAG, "Commissioning Complete");
        PlatformMgr().ScheduleWork(deinit_ble_if_commissioned, reinterpret_cast<intptr_t>(nullptr));
        break;

        case chip::DeviceLayer::DeviceEventType::kCHIPoBLEConnectionClosed:
            ESP_LOGI(TAG, "BLE Disconnected");
        break;
#endif
        default:
            break;
    }
}

esp_err_t matter_interface_init(const esp_matter::event_callback_t handle_chip_device_event,
                                const intptr_t callback_arg) {
    ESP_LOGI(TAG, "Initializing Matter stack");
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

    // Initialize CHIP stack
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

    // Register internal and optional external event handlers
    chip::DeviceLayer::PlatformMgr().AddEventHandler(device_callback_internal, 0);
    if (handle_chip_device_event) {
        chip::DeviceLayer::PlatformMgr().AddEventHandler(handle_chip_device_event, callback_arg);
    }

    esp_matter_started = true;

    return ESP_OK;
}

esp_err_t matter_interface_controller_init(const uint64_t node_id, const uint64_t fabric_id, const uint16_t listen_port) {
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "Initializing Matter controller client");
    esp_matter::lock::chip_stack_lock(portMAX_DELAY);

    err = esp_matter::controller::matter_controller_client::get_instance().init(node_id, fabric_id, listen_port);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Controller client initialization failed: 0x%x", err);
        goto exit;
    }

#ifdef CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
    ESP_LOGI(TAG, "Setting up commissioner");
    err = esp_matter::controller::matter_controller_client::get_instance().setup_commissioner();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Commissioner setup failed: 0x%x", err);
        goto exit;
    }
#endif

    exit:
        esp_matter::lock::chip_stack_unlock();
    return err;
}