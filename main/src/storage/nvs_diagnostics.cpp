#include "storage/nvs_diagnostics.h"

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <cstring>

static const char *TAG = "NVS_DIAGNOSTICS";

static const char *display_label(const char *label) {
    return label ? label : "nvs";
}

esp_err_t init_nvs_partition_or_recover(const char *label) {
    esp_err_t err = ESP_OK;

    if (label == nullptr) {
        err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "Default NVS requires erase: %s", esp_err_to_name(err));
            const esp_err_t erase_err = nvs_flash_erase();
            if (erase_err != ESP_OK) {
                return erase_err;
            }
            err = nvs_flash_init();
        }
        return err;
    }

    err = nvs_flash_init_partition(label);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition %s requires erase: %s", label, esp_err_to_name(err));
        const esp_err_t erase_err = nvs_flash_erase_partition(label);
        if (erase_err != ESP_OK) {
            return erase_err;
        }
        err = nvs_flash_init_partition(label);
    }

    return err;
}

esp_err_t read_nvs_diag(const char *label, nvs_diag_t *diag) {
    if (!diag) return ESP_ERR_INVALID_ARG;

    nvs_stats_t stats = {};
    const esp_err_t err = nvs_get_stats(label, &stats);

    diag->label = display_label(label);
    diag->used_entries = 0;
    diag->free_entries = 0;
    diag->total_entries = 0;
    diag->namespace_count = 0;
    diag->err = err;

    if (err != ESP_OK) {
        return err;
    }

    diag->used_entries = stats.used_entries;
    diag->free_entries = stats.free_entries;
    diag->total_entries = stats.total_entries;
    diag->namespace_count = stats.namespace_count;
    return ESP_OK;
}

void log_nvs_stats(const char *label) {
    nvs_diag_t diag = {};
    const esp_err_t err = read_nvs_diag(label, &diag);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS stats unavailable for %s: %s",
                 display_label(label), esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG,
             "NVS partition '%s': used=%u free=%u total=%u namespaces=%u",
             display_label(label),
             static_cast<unsigned>(diag.used_entries),
             static_cast<unsigned>(diag.free_entries),
             static_cast<unsigned>(diag.total_entries),
             static_cast<unsigned>(diag.namespace_count));
}

static bool add_partition_json(cJSON *root, const char *key, const char *label) {
    cJSON *partition = cJSON_AddObjectToObject(root, key);
    if (!partition) return false;

    nvs_diag_t diag = {};
    read_nvs_diag(label, &diag);

    cJSON_AddNumberToObject(partition, "used_entries", static_cast<double>(diag.used_entries));
    cJSON_AddNumberToObject(partition, "free_entries", static_cast<double>(diag.free_entries));
    cJSON_AddNumberToObject(partition, "total_entries", static_cast<double>(diag.total_entries));
    cJSON_AddNumberToObject(partition, "namespace_count", static_cast<double>(diag.namespace_count));
    if (diag.err == ESP_OK) {
        cJSON_AddNullToObject(partition, "error");
    } else {
        cJSON_AddStringToObject(partition, "error", esp_err_to_name(diag.err));
    }

    return true;
}

cJSON *nvs_diagnostics_to_json(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return nullptr;

    if (!add_partition_json(root, "nvs", nullptr) ||
        !add_partition_json(root, "ot_nvs", "ot_nvs") ||
        !add_partition_json(root, "matter_nvs", "matter_nvs")) {
        cJSON_Delete(root);
        return nullptr;
    }

    return root;
}

extern "C" esp_err_t __real_nvs_open(const char *namespace_name,
                                      nvs_open_mode_t open_mode,
                                      nvs_handle_t *out_handle);

extern "C" esp_err_t __wrap_nvs_open(const char *namespace_name,
                                      nvs_open_mode_t open_mode,
                                      nvs_handle_t *out_handle) {
    if (namespace_name && strcmp(namespace_name, OT_NVS_NAMESPACE) == 0) {
        return nvs_open_from_partition(OT_NVS_PARTITION_NAME, namespace_name, open_mode, out_handle);
    }

    return __real_nvs_open(namespace_name, open_mode, out_handle);
}
