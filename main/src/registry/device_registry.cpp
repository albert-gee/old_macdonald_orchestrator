#include "registry/device_registry.h"

#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <esp_check.h>
#include <nvs.h>
#include <nvs_flash.h>

static constexpr const char *NVS_NAMESPACE = "dev_registry";
static constexpr const char *NVS_KEY = "records";
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

static esp_err_t load_store(RegistryStore *store) {
    if (!store) return ESP_ERR_INVALID_ARG;
    memset(store, 0, sizeof(*store));
    store->version = 1;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    size_t len = sizeof(*store);
    err = nvs_get_blob(handle, NVS_KEY, store, &len);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(store, 0, sizeof(*store));
        store->version = 1;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;
    if (len != sizeof(*store) || store->count > MAX_RECORDS) {
        memset(store, 0, sizeof(*store));
        store->version = 1;
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

esp_err_t device_registry_init(void) {
    RegistryStore store;
    esp_err_t err = load_store(&store);
    if (err != ESP_OK) return err;
    return save_store(&store);
}

esp_err_t device_registry_upsert(const DeviceRecord *record) {
    if (!record || !record->device_id[0]) return ESP_ERR_INVALID_ARG;
    RegistryStore store;
    ESP_RETURN_ON_ERROR(load_store(&store), "DEVICE_REGISTRY", "load failed");

    for (uint32_t i = 0; i < store.count; ++i) {
        if (strncmp(store.records[i].device_id, record->device_id, DEVICE_REGISTRY_ID_MAX) == 0) {
            store.records[i] = *record;
            return save_store(&store);
        }
    }

    if (store.count >= MAX_RECORDS) return ESP_ERR_NO_MEM;
    store.records[store.count++] = *record;
    return save_store(&store);
}

esp_err_t device_registry_get(const char *device_id, DeviceRecord *record) {
    if (!device_id || !record) return ESP_ERR_INVALID_ARG;
    RegistryStore store;
    ESP_RETURN_ON_ERROR(load_store(&store), "DEVICE_REGISTRY", "load failed");
    for (uint32_t i = 0; i < store.count; ++i) {
        if (strncmp(store.records[i].device_id, device_id, DEVICE_REGISTRY_ID_MAX) == 0) {
            *record = store.records[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t device_registry_remove(const char *device_id) {
    if (!device_id) return ESP_ERR_INVALID_ARG;
    RegistryStore store;
    ESP_RETURN_ON_ERROR(load_store(&store), "DEVICE_REGISTRY", "load failed");
    for (uint32_t i = 0; i < store.count; ++i) {
        if (strncmp(store.records[i].device_id, device_id, DEVICE_REGISTRY_ID_MAX) == 0) {
            for (uint32_t j = i + 1; j < store.count; ++j) {
                store.records[j - 1] = store.records[j];
            }
            --store.count;
            return save_store(&store);
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t device_registry_rename(const char *device_id, const char *label) {
    if (!device_id || !label) return ESP_ERR_INVALID_ARG;
    RegistryStore store;
    ESP_RETURN_ON_ERROR(load_store(&store), "DEVICE_REGISTRY", "load failed");
    for (uint32_t i = 0; i < store.count; ++i) {
        if (strncmp(store.records[i].device_id, device_id, DEVICE_REGISTRY_ID_MAX) == 0) {
            copy_field(store.records[i].label, label, sizeof(store.records[i].label));
            return save_store(&store);
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static void add_record_json(cJSON *array, const DeviceRecord &record) {
    cJSON *item = cJSON_CreateObject();
    char node_id[24];
    char device_type_id[12];
    snprintf(node_id, sizeof(node_id), "%" PRIu64, record.node_id);
    snprintf(device_type_id, sizeof(device_type_id), "0x%04" PRIX32, record.device_type_id);
    cJSON_AddStringToObject(item, "device_id", record.device_id);
    cJSON_AddStringToObject(item, "node_id", node_id);
    cJSON_AddNumberToObject(item, "endpoint_id", record.endpoint_id);
    cJSON_AddStringToObject(item, "device_type_id", device_type_id);
    cJSON_AddStringToObject(item, "label", record.label);
    cJSON_AddBoolToObject(item, "reachable", record.reachable);
    cJSON_AddNumberToObject(item, "vendor_id", record.vendor_id);
    cJSON_AddNumberToObject(item, "product_id", record.product_id);
    cJSON_AddStringToObject(item, "product_name", record.product_name);
    cJSON_AddStringToObject(item, "location", record.location);
    cJSON_AddItemToArray(array, item);
}

cJSON *device_registry_to_json(void) {
    RegistryStore store;
    if (load_store(&store) != ESP_OK) return nullptr;
    cJSON *payload = cJSON_CreateObject();
    cJSON *devices = cJSON_AddArrayToObject(payload, "devices");
    for (uint32_t i = 0; i < store.count; ++i) {
        add_record_json(devices, store.records[i]);
    }
    return payload;
}
