#include "events/chip_event_handler.h"

#include <esp_log.h>
#include <portmacro.h>
#include <esp_matter.h>
#include <utils/event_handler_util.h>

static const char *TAG = "CHIP_EVENT_HANDLER";

void handle_chip_device_event(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg) {
    char message[100];

    switch (event->Type) {
        case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInterfaceIpAddressChanged:
            ESP_LOGI(TAG, "Interface IP Address changed");
            break;

        case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
            ESP_LOGI(TAG, "New Matter device commissioned! Node ID: 0x%llX, Fabric Index: %d",
                     event->CommissioningComplete.nodeId, event->CommissioningComplete.fabricIndex);
            snprintf(message, sizeof(message), "New Matter device commissioned! Node ID: 0x%llX, Fabric Index: %d",
                     event->CommissioningComplete.nodeId, event->CommissioningComplete.fabricIndex);
            broadcast_info_message(message);
            break;

        case chip::DeviceLayer::DeviceEventType::kServiceProvisioningChange:
            ESP_LOGI(TAG, "Service provisioning changed: Provisioned=%s, Config Updated=%s",
                     event->ServiceProvisioningChange.IsServiceProvisioned ? "Yes" : "No",
                     event->ServiceProvisioningChange.ServiceConfigUpdated ? "Yes" : "No");
            if (event->FabricMembershipChange.IsMemberOfFabric) {
                ESP_LOGI(TAG, "Device added to a Fabric.");
            } else {
                ESP_LOGI(TAG, "Device removed from a Fabric.");
            }
            break;

        case chip::DeviceLayer::DeviceEventType::kBindingsChangedViaCluster:
            ESP_LOGI(TAG, "Matter bindings updated on Fabric Index: %d",
                     event->BindingsChanged.fabricIndex);
            break;

        case chip::DeviceLayer::DeviceEventType::kSecureSessionEstablished:
            ESP_LOGI(
                TAG, "Secure session established with Node ID: 0x%llX, Session Key ID: %d, Type: %d, Commissioner: %s",
                event->SessionEstablished.PeerNodeId, event->SessionEstablished.SessionKeyId,
                event->SessionEstablished.SessionType,
                event->SessionEstablished.IsCommissioner ? "Yes" : "No");
            snprintf(message, sizeof(message), "Secure session established with Node ID 0x%llX",
                     event->SessionEstablished.PeerNodeId);
            broadcast_info_message(message);
            break;

        case chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange:
            ESP_LOGI(TAG, "Thread connectivity changed: %s",
                     event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established
                     ? "Connected"
                     : "Disconnected");
            break;

        case chip::DeviceLayer::DeviceEventType::kServerReady:
            ESP_LOGI(TAG, "Matter Server is ready.");
            break;

        case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:
            if (event->Platform.ESPSystemEvent.Base == IP_EVENT &&
                event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP) {
                ESP_LOGI(TAG, "IP Address assigned to Wi-Fi interface.");
            }
            break;

        default:
            break;
    }
}

static void process_tlv_data(TLVReader *data) {

    CHIP_ERROR err;
    char message[100];

    // Enter the container if needed (optional: depends on TLV structure)
    chip::TLV::TLVType container;
    err = data->EnterContainer(container);
    if (err == CHIP_NO_ERROR) {
        ESP_LOGI(TAG, "Received container: %u", container);

        // Iterate through the elements in the container
        while ((err = data->Next()) == CHIP_NO_ERROR) {
            chip::TLV::TLVType elementType = data->GetType();
            switch (elementType) {
                case chip::TLV::TLVType::kTLVType_UnsignedInteger: {
                    uint64_t unsigned_value;
                    data->Get(unsigned_value);
                    ESP_LOGI(TAG, "Unsigned integer: %" PRIu64, unsigned_value);
                    snprintf(message, sizeof(message), "Unsigned integer: %" PRIu64, unsigned_value);
                    broadcast_info_message(message);
                    break;
                }
                case chip::TLV::TLVType::kTLVType_SignedInteger: {
                    int64_t signed_value;
                    data->Get(signed_value);
                    ESP_LOGI(TAG, "Signed integer: %" PRId64, signed_value);
                    snprintf(message, sizeof(message), "Signed integer: %" PRId64, signed_value);
                    broadcast_info_message(message);
                    break;
                }
                case chip::TLV::TLVType::kTLVType_FloatingPointNumber: {
                    double float_value;
                    data->Get(float_value);
                    ESP_LOGI(TAG, "Floating point value: %f", float_value);
                    snprintf(message, sizeof(message), "Floating point value: %f", float_value);
                    broadcast_info_message(message);
                    break;
                }
                case chip::TLV::TLVType::kTLVType_UTF8String: {
                    char string_value[256];
                    data->GetString(string_value, sizeof(string_value));
                    ESP_LOGI(TAG, "String value: %s", string_value);
                    snprintf(message, sizeof(message), "String value: %s", string_value);
                    broadcast_info_message(message);
                    break;
                }
                case chip::TLV::TLVType::kTLVType_Structure: {
                    chip::TLV::TLVType innerContainerType;
                    err = data->EnterContainer(innerContainerType);
                    if (err == CHIP_NO_ERROR) {
                        // Iterate through the elements in the structure container
                        while ((err = data->Next()) == CHIP_NO_ERROR) {
                            chip::TLV::TLVType elementType = data->GetType();
                            switch (elementType) {
                                case chip::TLV::TLVType::kTLVType_UnsignedInteger: {
                                    uint64_t unsigned_value;
                                    data->Get(unsigned_value);
                                    ESP_LOGI(TAG, "Unsigned integer: %" PRIu64, unsigned_value);
                                    snprintf(message, sizeof(message), "Unsigned integer: %" PRIu64, unsigned_value);
                                    broadcast_info_message(message);
                                    break;
                                }
                                case chip::TLV::TLVType::kTLVType_SignedInteger: {
                                    int64_t signed_value;
                                    data->Get(signed_value);
                                    ESP_LOGI(TAG, "Signed integer: %" PRId64, signed_value);
                                    snprintf(message, sizeof(message), "Signed integer: %" PRId64, signed_value);
                                    broadcast_info_message(message);
                                    break;
                                }
                                case chip::TLV::TLVType::kTLVType_FloatingPointNumber: {
                                    double float_value;
                                    data->Get(float_value);
                                    ESP_LOGI(TAG, "Floating point value: %f", float_value);
                                    snprintf(message, sizeof(message), "Floating point value: %f", float_value);
                                    broadcast_info_message(message);
                                    break;
                                }
                                case chip::TLV::TLVType::kTLVType_UTF8String: {
                                    char string_value[256];
                                    data->GetString(string_value, sizeof(string_value));
                                    ESP_LOGI(TAG, "String value: %s", string_value);
                                    snprintf(message, sizeof(message), "String value: %s", string_value);
                                    broadcast_info_message(message);
                                    break;
                                }
                                default:
                                    ESP_LOGW(TAG, "Unsupported or unknown TLV type: %u", elementType);
                                    snprintf(message, sizeof(message), "Unsupported or unknown TLV type: %u",
                                             elementType);
                                    broadcast_info_message(message);
                                    break;
                            }
                        }

                        data->ExitContainer(innerContainerType);
                    } else {
                        ESP_LOGW(TAG, "Failed to enter structure container");
                        snprintf(message, sizeof(message), "Failed to enter structure container");
                        broadcast_info_message(message);
                    }
                    break;
                }
                default:
                    ESP_LOGW(TAG, "Unsupported or unknown TLV type: %u", elementType);
                    snprintf(message, sizeof(message), "Unsupported or unknown TLV type: %u", elementType);
                    broadcast_info_message(message);
                    break;
            }
        }

        data->ExitContainer(container);

        return;
    }

    // Don't call data->Next(), read the current value directly
    uint64_t unsigned_value = 0;
    err = data->Get(unsigned_value);
    if (err == CHIP_NO_ERROR) {
        ESP_LOGI(TAG, "Received unsigned integer: %" PRIu64, unsigned_value);
        snprintf(message, sizeof(message), "Received unsigned integer: %" PRIu64, unsigned_value);
        broadcast_info_message(message);
        return;
    }

    int64_t signed_value = 0;
    err = data->Get(signed_value);
    if (err == CHIP_NO_ERROR) {
        ESP_LOGI(TAG, "Received signed integer: %" PRId64, signed_value);
        snprintf(message, sizeof(message), "Received signed integer: %" PRId64, signed_value);
        broadcast_info_message(message);
        return;
    }

    double float_value = 0.0;
    err = data->Get(float_value);
    if (err == CHIP_NO_ERROR) {
        ESP_LOGI(TAG, "Received floating point value: %f", float_value);
        snprintf(message, sizeof(message), "Received floating point value: %f", float_value);
        broadcast_info_message(message);
        return;
    }

    char string_value[256];
    err = data->GetString(string_value, sizeof(string_value));
    if (err == CHIP_NO_ERROR) {
        ESP_LOGI(TAG, "Received string value: %s", string_value);
        snprintf(message, sizeof(message), "Received string value: %s", string_value);
        broadcast_info_message(message);
        return;
    }

    ESP_LOGW(TAG, "Unsupported or unknown TLV type");
}

void read_attribute_data_callback(uint64_t remote_node_id, const chip::app::ConcreteDataAttributePath &path,
                                  chip::TLV::TLVReader *data) {
    ESP_LOGI(TAG, "Received attribute report from node: %" PRIu64, remote_node_id);

    process_tlv_data(data);
}

void subscription_attribute_report_callback(uint64_t remote_node_id,
                                            const chip::app::ConcreteDataAttributePath &path,
                                            chip::TLV::TLVReader *data) {
    char message[256];
    snprintf(message, sizeof(message),
             "Attribute report from node 0x%llX - Endpoint: 0x%02X, Cluster: 0x%04X, Attribute: 0x%04X",
             remote_node_id, path.mEndpointId, path.mClusterId, path.mAttributeId);
    broadcast_info_message(message);
    ESP_LOGI(TAG, "%s", message);

    if (data) {
        process_tlv_data(data);
    }

    // If cluster ID is 0x402 and attribute ID is 0x0, broadcast temperature
    if (path.mClusterId == 0x0402 && path.mAttributeId == 0x0000) {
        int64_t signed_value = 0;
        CHIP_ERROR err = data->Get(signed_value);
        if (err == CHIP_NO_ERROR) {
            ESP_LOGI(TAG, "Received signed integer: %" PRId64, signed_value);
            snprintf(message, sizeof(message), "%" PRId64, signed_value);
            broadcast_temperature(message);
        } else {
            ESP_LOGE(TAG, "Failed to decode TLV signed integer: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }
}


void subscribe_done_callback(uint64_t remote_node_id, uint32_t subscription_id) {
    char message[128];
    snprintf(message, sizeof(message), "Subscription done for node 0x%llX with subscription ID: %u",
             remote_node_id, subscription_id);
    broadcast_info_message(message);
    ESP_LOGI(TAG, "%s", message);
}
