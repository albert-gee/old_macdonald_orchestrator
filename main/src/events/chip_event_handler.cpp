#include "events/chip_event_handler.h"

#include <esp_log.h>
#include <portmacro.h>
#include <esp_matter.h>

static const char *TAG = "CHIP_EVENT_HANDLER";

void handle_chip_device_event(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg) {
    switch (event->Type) {
        case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInterfaceIpAddressChanged:
            ESP_LOGI(TAG, "Interface IP Address changed");
            break;

        case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:

            if (event->Platform.ESPSystemEvent.Base == IP_EVENT && event->Platform.ESPSystemEvent.Id ==
                IP_EVENT_STA_GOT_IP) {
                // Wi-Fi STA got IP address. Start Thread Border Router

                ESP_LOGI(TAG, "IP Address assigned to Wi-Fi interface");
            }
            break;
        default:
            break;
    }
}
