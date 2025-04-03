#include "events/chip_event_handler.h"

#include <esp_log.h>
#include <portmacro.h>
#include <esp_matter.h>
#include <utils/event_handler_util.h>

static const char *TAG = "CHIP_EVENT_HANDLER";

void handle_chip_device_event(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg) {
    switch (event->Type) {
        case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInterfaceIpAddressChanged:
            ESP_LOGI(TAG, "Interface IP Address changed");
            break;

        // case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        //     ESP_LOGI(TAG, "New Matter device commissioned! Node ID: 0x%llX, Fabric Index: %d",
        //              event->CommissioningComplete.nodeId, event->CommissioningComplete.fabricIndex);
        //     break;
        //
        // case chip::DeviceLayer::DeviceEventType::kServiceProvisioningChange:
        //     ESP_LOGI(TAG, "Service provisioning changed: Provisioned=%s, Config Updated=%s",
        //              event->ServiceProvisioningChange.IsServiceProvisioned ? "Yes" : "No",
        //              event->ServiceProvisioningChange.ServiceConfigUpdated ? "Yes" : "No");
        //     if (event->FabricMembershipChange.IsMemberOfFabric) {
        //         ESP_LOGI(TAG, "Device added to a Fabric.");
        //     } else {
        //         ESP_LOGI(TAG, "Device removed from a Fabric.");
        //     }
        //     break;
        //
        // case chip::DeviceLayer::DeviceEventType::kBindingsChangedViaCluster:
        //     ESP_LOGI(TAG, "Matter bindings updated on Fabric Index: %d",
        //              event->BindingsChanged.fabricIndex);
        //     break;
        //
        // case chip::DeviceLayer::DeviceEventType::kSecureSessionEstablished:
        //     ESP_LOGI(
        //         TAG, "Secure session established with Node ID: 0x%llX, Session Key ID: %d, Type: %d, Commissioner: %s",
        //         event->SessionEstablished.PeerNodeId, event->SessionEstablished.SessionKeyId,
        //         event->SessionEstablished.SessionType,
        //         event->SessionEstablished.IsCommissioner ? "Yes" : "No");
        //     char message[100];
        //     snprintf(message, sizeof(message), "Secure session established with Node ID 0x%llX", event->SessionEstablished.PeerNodeId);
        //     broadcast_info_message(message);
        //     break;
        //
        // case chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange:
        //     ESP_LOGI(TAG, "Thread connectivity changed: %s",
        //              event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established
        //              ? "Connected"
        //              : "Disconnected");
        //     break;
        //
        // case chip::DeviceLayer::DeviceEventType::kServerReady:
        //     ESP_LOGI(TAG, "Matter Server is ready.");
        //     break;
        //
        // case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:
        //     if (event->Platform.ESPSystemEvent.Base == IP_EVENT &&
        //         event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP) {
        //         ESP_LOGI(TAG, "IP Address assigned to Wi-Fi interface.");
        //     }
        //     break;

        default:
            ESP_LOGW(TAG, "Unhandled CHIP event: %d", event->Type);
            break;
    }
}
