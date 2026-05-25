#include "registry/device_registry.h"

#include "state/orchestrator_state.h"

#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <cstdlib>
#include <esp_check.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

static constexpr const char *TAG = "DEVICE_REGISTRY";
static constexpr const char *NVS_NAMESPACE = "dev_registry";
static constexpr const char *NVS_KEY = "records";
static constexpr uint32_t REGISTRY_VERSION = 2;
static constexpr size_t MAX_RECORDS = 16;

struct RegistryStore {
    uint32_t version;
    uint32_t count;
    DeviceRecord records[MAX_RECORDS];
};

static void copy_field(char *dest, const char *src, size_t len) {
    if (!dest || len == 0) return;
    if (!src) src = "";
    strncpy(dest, src, len - 1);
    dest[len - 1] = '\0';
}

static void init_empty_store(RegistryStore *store) {
    memset(store, 0, sizeof(*store));
    store->version = REGISTRY_VERSION;
}

static RegistryStore *alloc_store(void) {
    return static_cast<RegistryStore *>(calloc(1, sizeof(RegistryStore)));
}

const char *device_registry_semantic_type_to_string(DeviceCapabilitySemanticType semantic_type) {
    switch (semantic_type) {
        case DEVICE_CAPABILITY_TEMPERATURE: return "temperature";
        case DEVICE_CAPABILITY_PRESSURE: return "pressure";
        case DEVICE_CAPABILITY_RELAY: return "relay";
        case DEVICE_CAPABILITY_RAW_ATTRIBUTE: return "raw_attribute";
        case DEVICE_CAPABILITY_RAW_COMMAND: return "raw_command";
        default: return "raw_attribute";
    }
}

bool device_registry_semantic_type_from_string(const char *semantic_type, DeviceCapabilitySemanticType *out) {
    if (!semantic_type || !out) return false;
    if (strcmp(semantic_type, "temperature") == 0) *out = DEVICE_CAPABILITY_TEMPERATURE;
    else if (strcmp(semantic_type, "pressure") == 0) *out = DEVICE_CAPABILITY_PRESSURE;
    else if (strcmp(semantic_type, "relay") == 0) *out = DEVICE_CAPABILITY_RELAY;
    else if (strcmp(semantic_type, "raw_attribute") == 0) *out = DEVICE_CAPABILITY_RAW_ATTRIBUTE;
    else if (strcmp(semantic_type, "raw_command") == 0) *out = DEVICE_CAPABILITY_RAW_COMMAND;
    else return false;
    return true;
}

static esp_err_t load_store(RegistryStore *store) {
    if (!store) return ESP_ERR_INVALID_ARG;
    init_empty_store(store);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    size_t len = sizeof(*store);
    err = nvs_get_blob(handle, NVS_KEY, store, &len);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        init_empty_store(store);
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    if (len != sizeof(*store) || store->version != REGISTRY_VERSION || store->count > MAX_RECORDS) {
        ESP_LOGW(TAG, "Resetting unsupported or invalid device registry blob");
        init_empty_store(store);
    }

    for (uint32_t i = 0; i < store->count; ++i) {
        if (store->records[i].capability_count > DEVICE_REGISTRY_CAPABILITY_MAX) {
            ESP_LOGW(TAG, "Resetting registry with invalid capability count");
            init_empty_store(store);
            break;
        }
    }
    return ESP_OK;
}

static esp_err_t save_store(const RegistryStore *store) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, NVS_KEY, store, sizeof(*store));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t save_and_notify(const RegistryStore *store) {
    ESP_RETURN_ON_ERROR(save_store(store), TAG, "save failed");
    orchestrator_state_broadcast_event("device.registry_changed", nullptr);
    orchestrator_state_broadcast_snapshot();
    return ESP_OK;
}

esp_err_t device_registry_init(void) {
    RegistryStore *store = alloc_store();
    if (!store) return ESP_ERR_NO_MEM;
    esp_err_t err = load_store(store);
    if (err == ESP_OK) err = save_store(store);
    if (err == ESP_OK) ESP_LOGI(TAG, "Initialized device registry with %" PRIu32 " device(s)", store->count);
    free(store);
    return err;
}

esp_err_t device_registry_upsert_device(const DeviceRecord *record) {
    if (!record || !record->device_id[0] || record->capability_count > DEVICE_REGISTRY_CAPABILITY_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    RegistryStore *store = alloc_store();
    if (!store) return ESP_ERR_NO_MEM;
    esp_err_t err = load_store(store);
    if (err != ESP_OK) {
        free(store);
        return err;
    }

    for (uint32_t i = 0; i < store->count; ++i) {
        if (strncmp(store->records[i].device_id, record->device_id, DEVICE_REGISTRY_ID_MAX) == 0) {
            store->records[i] = *record;
            err = save_and_notify(store);
            free(store);
            return err;
        }
    }

    if (store->count >= MAX_RECORDS) {
        free(store);
        return ESP_ERR_NO_MEM;
    }
    store->records[store->count++] = *record;
    err = save_and_notify(store);
    free(store);
    return err;
}

esp_err_t device_registry_get_device(const char *device_id, DeviceRecord *record) {
    if (!device_id || !record) return ESP_ERR_INVALID_ARG;
    RegistryStore *store = alloc_store();
    if (!store) return ESP_ERR_NO_MEM;
    esp_err_t err = load_store(store);
    if (err != ESP_OK) {
        free(store);
        return err;
    }
    for (uint32_t i = 0; i < store->count; ++i) {
        if (strncmp(store->records[i].device_id, device_id, DEVICE_REGISTRY_ID_MAX) == 0) {
            *record = store->records[i];
            free(store);
            return ESP_OK;
        }
    }
    free(store);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t device_registry_remove_device(const char *device_id) {
    if (!device_id) return ESP_ERR_INVALID_ARG;
    RegistryStore *store = alloc_store();
    if (!store) return ESP_ERR_NO_MEM;
    esp_err_t err = load_store(store);
    if (err != ESP_OK) {
        free(store);
        return err;
    }
    for (uint32_t i = 0; i < store->count; ++i) {
        if (strncmp(store->records[i].device_id, device_id, DEVICE_REGISTRY_ID_MAX) == 0) {
            for (uint32_t j = i + 1; j < store->count; ++j) {
                store->records[j - 1] = store->records[j];
            }
            --store->count;
            err = save_and_notify(store);
            free(store);
            return err;
        }
    }
    free(store);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t device_registry_rename_device(const char *device_id, const char *label) {
    if (!device_id || !label) return ESP_ERR_INVALID_ARG;
    RegistryStore *store = alloc_store();
    if (!store) return ESP_ERR_NO_MEM;
    esp_err_t err = load_store(store);
    if (err != ESP_OK) {
        free(store);
        return err;
    }
    for (uint32_t i = 0; i < store->count; ++i) {
        if (strncmp(store->records[i].device_id, device_id, DEVICE_REGISTRY_ID_MAX) == 0) {
            copy_field(store->records[i].label, label, sizeof(store->records[i].label));
            err = save_and_notify(store);
            free(store);
            return err;
        }
    }
    free(store);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t device_registry_find_capability(const char *device_id,
                                          DeviceCapabilitySemanticType semantic_type,
                                          DeviceRecord *record,
                                          DeviceCapability *capability) {
    if (!device_id || !capability) return ESP_ERR_INVALID_ARG;
    DeviceRecord found = {};
    ESP_RETURN_ON_ERROR(device_registry_get_device(device_id, &found), TAG, "device not found");
    for (uint32_t i = 0; i < found.capability_count; ++i) {
        if (found.capabilities[i].semantic_type == semantic_type) {
            if (record) *record = found;
            *capability = found.capabilities[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t device_registry_find_capability_by_path(uint64_t node_id,
                                                  uint16_t endpoint_id,
                                                  uint32_t cluster_id,
                                                  uint32_t attribute_id,
                                                  DeviceRecord *record,
                                                  DeviceCapability *capability) {
    if (!capability) return ESP_ERR_INVALID_ARG;
    RegistryStore *store = alloc_store();
    if (!store) return ESP_ERR_NO_MEM;
    esp_err_t err = load_store(store);
    if (err != ESP_OK) {
        free(store);
        return err;
    }
    for (uint32_t i = 0; i < store->count; ++i) {
        if (store->records[i].node_id != node_id) continue;
        for (uint32_t j = 0; j < store->records[i].capability_count; ++j) {
            const DeviceCapability &candidate = store->records[i].capabilities[j];
            if (candidate.endpoint_id == endpoint_id &&
                candidate.cluster_id == cluster_id &&
                candidate.attribute_id == attribute_id) {
                if (record) *record = store->records[i];
                *capability = candidate;
                free(store);
                return ESP_OK;
            }
        }
    }
    free(store);
    return ESP_ERR_NOT_FOUND;
}

static bool add_number_if_relevant(cJSON *item, const char *name, uint32_t value) {
    return cJSON_AddNumberToObject(item, name, value) != nullptr;
}

static cJSON *capability_to_json(const DeviceCapability &capability) {
    cJSON *item = cJSON_CreateObject();
    if (!item) return nullptr;
    cJSON_AddStringToObject(item, "capability_id", capability.capability_id);
    cJSON_AddStringToObject(item, "semantic_type", device_registry_semantic_type_to_string(capability.semantic_type));
    cJSON_AddNumberToObject(item, "endpoint_id", capability.endpoint_id);
    cJSON_AddNumberToObject(item, "cluster_id", capability.cluster_id);
    if (capability.semantic_type != DEVICE_CAPABILITY_RAW_COMMAND && capability.attribute_id != UINT32_MAX) {
        add_number_if_relevant(item, "attribute_id", capability.attribute_id);
    }
    if ((capability.semantic_type == DEVICE_CAPABILITY_RELAY ||
         capability.semantic_type == DEVICE_CAPABILITY_RAW_COMMAND) &&
        capability.command_id != UINT32_MAX) {
        add_number_if_relevant(item, "command_id", capability.command_id);
    }
    cJSON_AddStringToObject(item, "label", capability.label);
    return item;
}

static cJSON *record_to_json(const DeviceRecord &record) {
    cJSON *item = cJSON_CreateObject();
    if (!item) return nullptr;

    char node_id[24];
    snprintf(node_id, sizeof(node_id), "%" PRIu64, record.node_id);
    cJSON_AddStringToObject(item, "device_id", record.device_id);
    cJSON_AddStringToObject(item, "node_id", node_id);
    cJSON_AddStringToObject(item, "label", record.label);
    cJSON_AddBoolToObject(item, "reachable", record.reachable);
    cJSON_AddNumberToObject(item, "vendor_id", record.vendor_id);
    cJSON_AddNumberToObject(item, "product_id", record.product_id);
    cJSON_AddStringToObject(item, "product_name", record.product_name);
    cJSON_AddStringToObject(item, "location", record.location);

    cJSON *capabilities = cJSON_AddArrayToObject(item, "capabilities");
    if (!capabilities) {
        cJSON_Delete(item);
        return nullptr;
    }
    for (uint32_t i = 0; i < record.capability_count; ++i) {
        cJSON *capability = capability_to_json(record.capabilities[i]);
        if (!capability) {
            cJSON_Delete(item);
            return nullptr;
        }
        cJSON_AddItemToArray(capabilities, capability);
    }

    return item;
}

cJSON *device_registry_to_json(void) {
    RegistryStore *store = alloc_store();
    if (!store) return nullptr;
    if (load_store(store) != ESP_OK) {
        free(store);
        return nullptr;
    }
    cJSON *payload = cJSON_CreateObject();
    if (!payload) {
        free(store);
        return nullptr;
    }
    cJSON *devices = cJSON_AddArrayToObject(payload, "devices");
    if (!devices) {
        cJSON_Delete(payload);
        free(store);
        return nullptr;
    }
    for (uint32_t i = 0; i < store->count; ++i) {
        cJSON *item = record_to_json(store->records[i]);
        if (!item) {
            cJSON_Delete(payload);
            free(store);
            return nullptr;
        }
        cJSON_AddItemToArray(devices, item);
    }
    free(store);
    return payload;
}
