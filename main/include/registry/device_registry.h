#ifndef DEVICE_REGISTRY_H
#define DEVICE_REGISTRY_H

#include <cJSON.h>
#include <esp_err.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_REGISTRY_ID_MAX 32
#define DEVICE_REGISTRY_LABEL_MAX 32
#define DEVICE_REGISTRY_PRODUCT_MAX 32
#define DEVICE_REGISTRY_LOCATION_MAX 32
#define DEVICE_REGISTRY_CAPABILITY_MAX 8

enum DeviceCapabilitySemanticType {
    DEVICE_CAPABILITY_TEMPERATURE = 0,
    DEVICE_CAPABILITY_PRESSURE,
    DEVICE_CAPABILITY_RELAY,
    DEVICE_CAPABILITY_RAW_ATTRIBUTE,
    DEVICE_CAPABILITY_RAW_COMMAND,
};

struct DeviceCapability {
    char capability_id[DEVICE_REGISTRY_ID_MAX];
    DeviceCapabilitySemanticType semantic_type;
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t attribute_id;
    uint32_t command_id;
    char label[DEVICE_REGISTRY_LABEL_MAX];
};

struct DeviceRecord {
    char device_id[DEVICE_REGISTRY_ID_MAX];
    uint64_t node_id;
    char label[DEVICE_REGISTRY_LABEL_MAX];
    bool reachable;
    uint32_t vendor_id;
    uint32_t product_id;
    char product_name[DEVICE_REGISTRY_PRODUCT_MAX];
    char location[DEVICE_REGISTRY_LOCATION_MAX];
    uint32_t capability_count;
    DeviceCapability capabilities[DEVICE_REGISTRY_CAPABILITY_MAX];
};

esp_err_t device_registry_init(void);
esp_err_t device_registry_upsert_device(const DeviceRecord *record);
esp_err_t device_registry_get_device(const char *device_id, DeviceRecord *record);
esp_err_t device_registry_remove_device(const char *device_id);
esp_err_t device_registry_rename_device(const char *device_id, const char *label);
esp_err_t device_registry_find_capability(const char *device_id,
                                          DeviceCapabilitySemanticType semantic_type,
                                          DeviceRecord *record,
                                          DeviceCapability *capability);
esp_err_t device_registry_find_capability_by_path(uint64_t node_id,
                                                  uint16_t endpoint_id,
                                                  uint32_t cluster_id,
                                                  uint32_t attribute_id,
                                                  DeviceRecord *record,
                                                  DeviceCapability *capability);
const char *device_registry_semantic_type_to_string(DeviceCapabilitySemanticType semantic_type);
bool device_registry_semantic_type_from_string(const char *semantic_type, DeviceCapabilitySemanticType *out);
cJSON *device_registry_to_json(void);

#ifdef __cplusplus
}
#endif

#endif
