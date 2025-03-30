#include "events/chip_event_handler.h"
#include "events/event_handler_util.h"

#include <esp_log.h>
#include <portmacro.h>

#include <esp_matter.h>

#if CONFIG_OPENTHREAD_BORDER_ROUTER
#include <esp_openthread_border_router.h>
#include <esp_openthread_lock.h>
#include <esp_spiffs.h>
// #include <platform/ESP32/OpenthreadLauncher.h>
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER

// #include <OpenthreadLauncher.h>

#if CONFIG_OPENTHREAD_BORDER_ROUTER
namespace chip::DeviceLayer {
    struct ChipDeviceEvent;
}

static bool sThreadBRInitialized = false;
#endif

static const char *TAG = "CHIP_EVENT_HANDLER";

void handle_chip_device_event(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg) {

    switch (event->Type) {
        case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInterfaceIpAddressChanged:
            ESP_LOGI(TAG, "Interface IP Address changed");
        break;

        case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:

            if (event->Platform.ESPSystemEvent.Base == IP_EVENT && event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP) {
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