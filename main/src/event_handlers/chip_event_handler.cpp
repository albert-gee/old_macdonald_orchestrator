#include "event_handlers/chip_event_handler.h"

#include <esp_log.h>
#include <portmacro.h>
#include <esp_matter.h>

#include "json/json_outbound_message.h"

static const char *TAG = "CHIP_EVENT_HANDLER";

void handle_chip_device_event(const ChipDeviceEvent *event, intptr_t arg) {
    switch (event->Type) {
        case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInterfaceIpAddressChanged:
            ESP_LOGI(TAG, "Interface IP Address changed");
            return;

        case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
            ESP_LOGI(TAG, "New Matter device commissioned! Node ID: 0x%llX, Fabric Index: %d",
                     event->CommissioningComplete.nodeId, event->CommissioningComplete.fabricIndex);
            create_info_matter_commissioning_complete_message(
                event->CommissioningComplete.nodeId,
                event->CommissioningComplete.fabricIndex);
            return;

        case chip::DeviceLayer::DeviceEventType::kServiceProvisioningChange:
            ESP_LOGI(TAG, "Service provisioning changed: Provisioned=%s, Config Updated=%s",
                     event->ServiceProvisioningChange.IsServiceProvisioned ? "Yes" : "No",
                     event->ServiceProvisioningChange.ServiceConfigUpdated ? "Yes" : "No");

            ESP_LOGI(TAG, "Fabric membership: %s",
                     event->FabricMembershipChange.IsMemberOfFabric ? "Added to fabric" : "Removed from fabric");
            return;

        case chip::DeviceLayer::DeviceEventType::kBindingsChangedViaCluster:
            ESP_LOGI(TAG, "Matter bindings updated on Fabric Index: %d",
                     event->BindingsChanged.fabricIndex);
            return;

        case chip::DeviceLayer::DeviceEventType::kSecureSessionEstablished:
            ESP_LOGI(TAG,
                     "Secure session established with Node ID: 0x%llX, Session Key ID: %d, Type: %d, Commissioner: %s",
                     event->SessionEstablished.PeerNodeId,
                     event->SessionEstablished.SessionKeyId,
                     event->SessionEstablished.SessionType,
                     event->SessionEstablished.IsCommissioner ? "Yes" : "No");
            return;

        case chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange:
            ESP_LOGI(TAG, "Thread connectivity changed: %s",
                     event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established
                         ? "Connected"
                         : "Disconnected");
            return;

        case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:
            if (event->Platform.ESPSystemEvent.Base == IP_EVENT &&
                event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP) {
                ESP_LOGI(TAG, "IP Address assigned to Wi-Fi interface.");
            }
            return;

        default:
            return;
    }
}

void attribute_data_report_callback(uint64_t remote_node_id, const chip::app::ConcreteDataAttributePath &path,
                                    chip::TLV::TLVReader *data) {
    ESP_LOGI(TAG, "Received attribute report from node: %" PRIu64, remote_node_id);

    char value_str[256] = {};
    bool value_extracted = false;

    chip::TLV::TLVType container;
    if (data->EnterContainer(container) == CHIP_NO_ERROR) {
        while (data->Next() == CHIP_NO_ERROR) {
            switch (data->GetType()) {
                case chip::TLV::kTLVType_UnsignedInteger: {
                    uint64_t val;
                    if (data->Get(val) == CHIP_NO_ERROR) {
                        snprintf(value_str, sizeof(value_str), "%" PRIu64, val);
                        value_extracted = true;
                    }
                    break;
                }
                case chip::TLV::kTLVType_SignedInteger: {
                    int64_t val;
                    if (data->Get(val) == CHIP_NO_ERROR) {
                        snprintf(value_str, sizeof(value_str), "%" PRId64, val);
                        value_extracted = true;
                    }
                    break;
                }
                case chip::TLV::kTLVType_FloatingPointNumber: {
                    double val;
                    if (data->Get(val) == CHIP_NO_ERROR) {
                        snprintf(value_str, sizeof(value_str), "%f", val);
                        value_extracted = true;
                    }
                    break;
                }
                case chip::TLV::kTLVType_UTF8String: {
                    if (data->GetString(value_str, sizeof(value_str)) == CHIP_NO_ERROR) {
                        value_extracted = true;
                    }
                    break;
                }
                default:
                    break;
            }
            if (value_extracted) break;
        }
        data->ExitContainer(container);
    } else {
        switch (data->GetType()) {
            case chip::TLV::kTLVType_UnsignedInteger: {
                uint64_t val;
                if (data->Get(val) == CHIP_NO_ERROR) {
                    snprintf(value_str, sizeof(value_str), "%" PRIu64, val);
                    value_extracted = true;
                }
                break;
            }
            case chip::TLV::kTLVType_SignedInteger: {
                int64_t val;
                if (data->Get(val) == CHIP_NO_ERROR) {
                    snprintf(value_str, sizeof(value_str), "%" PRId64, val);
                    value_extracted = true;
                }
                break;
            }
            case chip::TLV::kTLVType_FloatingPointNumber: {
                double val;
                if (data->Get(val) == CHIP_NO_ERROR) {
                    snprintf(value_str, sizeof(value_str), "%f", val);
                    value_extracted = true;
                }
                break;
            }
            case chip::TLV::kTLVType_UTF8String: {
                if (data->GetString(value_str, sizeof(value_str)) == CHIP_NO_ERROR) {
                    value_extracted = true;
                }
                break;
            }
            default:
                break;
        }
    }

    if (!value_extracted) {
        ESP_LOGW(TAG, "No attribute value could be extracted");
        return;
    }

    create_info_matter_attribute_report_message(
        remote_node_id,
        path.mEndpointId,
        path.mClusterId,
        path.mAttributeId,
        value_str
    );
}

void subscribe_done_callback(uint64_t remote_node_id, uint32_t subscription_id) {
    ESP_LOGI(TAG, "Subscription done for node 0x%llX with subscription ID: %u",
             remote_node_id, subscription_id);

    create_info_matter_subscribe_done_message(remote_node_id, subscription_id);
}
