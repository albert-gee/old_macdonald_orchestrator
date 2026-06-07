#include "matter/matter_discovery.h"

#include "commands/matter_commands.h"
#include "registry/device_registry.h"
#include "state/orchestrator_state.h"

#include <cstring>
#include <cstdio>
#include <esp_check.h>
#include <esp_log.h>
#include <inttypes.h>

static constexpr const char *TAG = "MATTER_DISCOVERY";

static constexpr uint32_t CLUSTER_DESCRIPTOR = 0x001D;
static constexpr uint32_t CLUSTER_BASIC_INFORMATION = 0x0028;
static constexpr uint32_t CLUSTER_ON_OFF = 0x0006;
static constexpr uint32_t CLUSTER_TEMPERATURE_MEASUREMENT = 0x0402;
static constexpr uint32_t CLUSTER_PRESSURE_MEASUREMENT = 0x0403;

static constexpr uint32_t ATTR_DEVICE_TYPE_LIST = 0x0000;
static constexpr uint32_t ATTR_SERVER_LIST = 0x0001;
static constexpr uint32_t ATTR_CLIENT_LIST = 0x0002;
static constexpr uint32_t ATTR_PARTS_LIST = 0x0003;
static constexpr uint32_t ATTR_MEASURED_VALUE = 0x0000;
static constexpr uint32_t ATTR_ON_OFF = 0x0000;
static constexpr uint32_t ATTR_VENDOR_ID = 0x0002;
static constexpr uint32_t ATTR_PRODUCT_NAME = 0x0003;
static constexpr uint32_t ATTR_PRODUCT_ID = 0x0004;
static constexpr uint32_t ATTR_NODE_LABEL = 0x0005;

static void copy_field(char *dest, const char *src, size_t len) {
    if (!dest || len == 0) return;
    if (!src) src = "";
    strncpy(dest, src, len - 1);
    dest[len - 1] = '\0';
}

static bool read_uint(chip::TLV::TLVReader &reader, uint64_t *out) {
    if (!out) return false;
    if (reader.GetType() == chip::TLV::kTLVType_UnsignedInteger) {
        return reader.Get(*out) == CHIP_NO_ERROR;
    }
    if (reader.GetType() == chip::TLV::kTLVType_SignedInteger) {
        int64_t val = 0;
        if (reader.Get(val) != CHIP_NO_ERROR || val < 0) return false;
        *out = static_cast<uint64_t>(val);
        return true;
    }
    return false;
}

static bool read_string(chip::TLV::TLVReader &reader, char *out, size_t out_len) {
    if (!out || out_len == 0 || reader.GetType() != chip::TLV::kTLVType_UTF8String) return false;
    return reader.GetString(out, out_len) == CHIP_NO_ERROR;
}

static uint32_t parse_uint_list(chip::TLV::TLVReader &reader, uint32_t *values, uint32_t max_values) {
    if (!values || max_values == 0) return 0;
    uint32_t count = 0;
    chip::TLV::TLVType container;
    if (reader.EnterContainer(container) != CHIP_NO_ERROR) return 0;
    while (count < max_values && reader.Next() == CHIP_NO_ERROR) {
        uint64_t value = 0;
        if (read_uint(reader, &value) && value <= UINT32_MAX) {
            values[count++] = static_cast<uint32_t>(value);
        }
    }
    reader.ExitContainer(container);
    return count;
}

static bool has_capability(const DeviceRecord &record, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id) {
    for (uint32_t i = 0; i < record.capability_count; ++i) {
        const DeviceCapability &capability = record.capabilities[i];
        if (capability.endpoint_id == endpoint_id &&
            capability.cluster_id == cluster_id &&
            capability.attribute_id == attribute_id) {
            return true;
        }
    }
    return false;
}

static esp_err_t append_capability(DeviceRecord *record,
                                   uint16_t endpoint_id,
                                   uint32_t cluster_id,
                                   DeviceCapabilitySemanticType semantic_type,
                                   const char *suffix,
                                   const char *label) {
    if (!record || !suffix || !label) return ESP_ERR_INVALID_ARG;
    if (has_capability(*record, endpoint_id, cluster_id, semantic_type == DEVICE_CAPABILITY_RELAY ? ATTR_ON_OFF : ATTR_MEASURED_VALUE)) {
        return ESP_OK;
    }
    if (record->capability_count >= DEVICE_REGISTRY_CAPABILITY_MAX) return ESP_ERR_NO_MEM;

    DeviceCapability &capability = record->capabilities[record->capability_count++];
    memset(&capability, 0, sizeof(capability));
    char device_id[DEVICE_REGISTRY_ID_MAX] = {};
    copy_field(device_id, record->device_id, sizeof(device_id));
    snprintf(capability.capability_id, sizeof(capability.capability_id), "%s-ep%u-%s",
             device_id, endpoint_id, suffix);
    capability.semantic_type = semantic_type;
    capability.endpoint_id = endpoint_id;
    capability.cluster_id = cluster_id;
    capability.attribute_id = semantic_type == DEVICE_CAPABILITY_RELAY ? ATTR_ON_OFF : ATTR_MEASURED_VALUE;
    capability.command_id = semantic_type == DEVICE_CAPABILITY_RELAY ? 0x01 : UINT32_MAX;
    copy_field(capability.label, label, sizeof(capability.label));
    return ESP_OK;
}

static esp_err_t apply_server_list(uint64_t node_id, uint16_t endpoint_id, const uint32_t *clusters, uint32_t cluster_count) {
    DeviceRecord record = {};
    ESP_RETURN_ON_ERROR(device_registry_get_device_by_node_id(node_id, &record), TAG, "device not in registry");
    bool changed = false;
    for (uint32_t i = 0; i < cluster_count; ++i) {
        const uint32_t before = record.capability_count;
        esp_err_t err = ESP_OK;
        if (clusters[i] == CLUSTER_TEMPERATURE_MEASUREMENT) {
            err = append_capability(&record, endpoint_id, clusters[i], DEVICE_CAPABILITY_TEMPERATURE,
                                    "temperature", "Temperature");
        } else if (clusters[i] == CLUSTER_PRESSURE_MEASUREMENT) {
            err = append_capability(&record, endpoint_id, clusters[i], DEVICE_CAPABILITY_PRESSURE,
                                    "pressure", "Pressure");
        } else if (clusters[i] == CLUSTER_ON_OFF) {
            err = append_capability(&record, endpoint_id, clusters[i], DEVICE_CAPABILITY_RELAY,
                                    "onoff", "On/Off actuator");
        }
        if (err != ESP_OK) return err;
        changed = changed || record.capability_count != before;
    }
    if (!changed) return ESP_OK;
    ESP_LOGI(TAG, "Discovered %" PRIu32 " capability/capabilities for node %" PRIu64,
             record.capability_count, node_id);
    ESP_RETURN_ON_ERROR(device_registry_upsert_device(&record), TAG, "registry update failed");
    orchestrator_state_broadcast_event("matter.discovery_complete", nullptr);
    return ESP_OK;
}

static void update_basic_information(uint64_t node_id,
                                     uint32_t attribute_id,
                                     chip::TLV::TLVReader &reader) {
    DeviceRecord record = {};
    if (device_registry_get_device_by_node_id(node_id, &record) != ESP_OK) return;

    bool changed = false;
    uint64_t uint_value = 0;
    char string_value[DEVICE_REGISTRY_PRODUCT_MAX] = {};
    if (attribute_id == ATTR_VENDOR_ID && read_uint(reader, &uint_value)) {
        record.vendor_id = static_cast<uint32_t>(uint_value);
        changed = true;
    } else if (attribute_id == ATTR_PRODUCT_ID && read_uint(reader, &uint_value)) {
        record.product_id = static_cast<uint32_t>(uint_value);
        changed = true;
    } else if (attribute_id == ATTR_PRODUCT_NAME && read_string(reader, string_value, sizeof(string_value))) {
        copy_field(record.product_name, string_value, sizeof(record.product_name));
        changed = true;
    } else if (attribute_id == ATTR_NODE_LABEL && read_string(reader, string_value, sizeof(string_value))) {
        copy_field(record.label, string_value, sizeof(record.label));
        changed = true;
    }

    if (changed) {
        device_registry_upsert_device(&record);
    }
}

esp_err_t matter_discovery_refresh_device(const char *device_id) {
    DeviceRecord record = {};
    ESP_RETURN_ON_ERROR(device_registry_get_device(device_id, &record), TAG, "device not found");
    ESP_LOGI(TAG, "Starting discovery for %s / node %" PRIu64, device_id, record.node_id);

    esp_err_t err = execute_attr_read_command(record.node_id, 0, CLUSTER_DESCRIPTOR, ATTR_PARTS_LIST);
    if (err != ESP_OK) return err;
    execute_attr_read_command(record.node_id, 0, CLUSTER_BASIC_INFORMATION, ATTR_VENDOR_ID);
    execute_attr_read_command(record.node_id, 0, CLUSTER_BASIC_INFORMATION, ATTR_PRODUCT_ID);
    execute_attr_read_command(record.node_id, 0, CLUSTER_BASIC_INFORMATION, ATTR_PRODUCT_NAME);
    execute_attr_read_command(record.node_id, 0, CLUSTER_BASIC_INFORMATION, ATTR_NODE_LABEL);
    return ESP_OK;
}

void matter_discovery_handle_attribute_report(uint64_t node_id,
                                              const chip::app::ConcreteDataAttributePath &path,
                                              chip::TLV::TLVReader *data) {
    if (!data) return;

    chip::TLV::TLVReader reader = *data;
    if (path.mClusterId == CLUSTER_BASIC_INFORMATION && path.mEndpointId == 0) {
        update_basic_information(node_id, path.mAttributeId, reader);
        return;
    }

    if (path.mClusterId != CLUSTER_DESCRIPTOR) return;

    if (path.mEndpointId == 0 && path.mAttributeId == ATTR_PARTS_LIST) {
        uint32_t endpoints[DEVICE_REGISTRY_CAPABILITY_MAX] = {};
        const uint32_t count = parse_uint_list(reader, endpoints, DEVICE_REGISTRY_CAPABILITY_MAX);
        ESP_LOGI(TAG, "Descriptor PartsList for node %" PRIu64 " has %" PRIu32 " endpoint(s)", node_id, count);
        for (uint32_t i = 0; i < count; ++i) {
            execute_attr_read_command(node_id, static_cast<uint16_t>(endpoints[i]), CLUSTER_DESCRIPTOR, ATTR_SERVER_LIST);
            execute_attr_read_command(node_id, static_cast<uint16_t>(endpoints[i]), CLUSTER_DESCRIPTOR, ATTR_CLIENT_LIST);
            execute_attr_read_command(node_id, static_cast<uint16_t>(endpoints[i]), CLUSTER_DESCRIPTOR, ATTR_DEVICE_TYPE_LIST);
        }
        return;
    }

    if (path.mAttributeId == ATTR_SERVER_LIST) {
        uint32_t clusters[DEVICE_REGISTRY_CAPABILITY_MAX * 2] = {};
        const uint32_t count = parse_uint_list(reader, clusters, DEVICE_REGISTRY_CAPABILITY_MAX * 2);
        if (count > 0) {
            apply_server_list(node_id, path.mEndpointId, clusters, count);
        }
    }
}
