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

struct DeviceRecord {
    char device_id[DEVICE_REGISTRY_ID_MAX];
    uint64_t node_id;
    uint16_t endpoint_id;
    uint32_t device_type_id;
    char label[DEVICE_REGISTRY_LABEL_MAX];
    bool reachable;
    uint32_t vendor_id;
    uint32_t product_id;
    char product_name[DEVICE_REGISTRY_PRODUCT_MAX];
    char location[DEVICE_REGISTRY_LOCATION_MAX];
};

esp_err_t device_registry_init(void);
esp_err_t device_registry_upsert(const DeviceRecord *record);
esp_err_t device_registry_get(const char *device_id, DeviceRecord *record);
esp_err_t device_registry_remove(const char *device_id);
esp_err_t device_registry_rename(const char *device_id, const char *label);
cJSON *device_registry_to_json(void);

#ifdef __cplusplus
}
#endif

#endif
