#include "matter_interface.h"

#include <cinttypes>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_core.h>
#include <esp_matter_ota.h>
#include <esp_matter_providers.h>
#include <esp_system.h>
#include <lib/core/ErrorStr.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <sdkconfig.h>
#include <string.h>

static const char *TAG = "MATTER_INTERFACE";

static bool esp_matter_started = false;
static char last_matter_error[192] = {};

struct MatterNamespace {
    const char *partition;
    const char *name;
};

static constexpr MatterNamespace kMatterNamespaces[] = {
    {CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL, "chip-factory"},
    {CONFIG_CHIP_CONFIG_NAMESPACE_PARTITION_LABEL, "chip-config"},
    {CONFIG_CHIP_COUNTERS_NAMESPACE_PARTITION_LABEL, "chip-counters"},
    {CONFIG_CHIP_KVS_NAMESPACE_PARTITION_LABEL, "CHIP_KVS"},
    {CONFIG_ESP_MATTER_NVS_PART_NAME, "esp_matter_kvs"},
    {CONFIG_ESP_MATTER_NVS_PART_NAME, "node"},
};

static void set_last_error(const char *message) {
    if (!message) {
        last_matter_error[0] = '\0';
        return;
    }
    snprintf(last_matter_error, sizeof(last_matter_error), "%s", message);
}

static void set_last_chip_error(const char *call, const CHIP_ERROR error) {
    const uint32_t raw_error = static_cast<uint32_t>(error.AsInteger());
    snprintf(last_matter_error, sizeof(last_matter_error), "%s failed: chip_error=%" PRIu32
             " 0x%08" PRIx32 " %s",
             call, raw_error, raw_error, chip::ErrorStr(error));
}

static void log_heap(const char *stage) {
    ESP_LOGI(TAG, "Heap %s: free=%" PRIu32 " min_free=%" PRIu32 " largest_8bit=%" PRIu32,
             stage,
             static_cast<uint32_t>(esp_get_free_heap_size()),
             static_cast<uint32_t>(esp_get_minimum_free_heap_size()),
             static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

static void log_chip_error(const char *call, const CHIP_ERROR error) {
    const uint32_t raw_error = static_cast<uint32_t>(error.AsInteger());
    set_last_chip_error(call, error);
    ESP_LOGE(TAG, "%s failed: chip_error=%" PRIu32 " hex=0x%08" PRIx32 " text=%s",
             call, raw_error, raw_error, chip::ErrorStr(error));
    ESP_LOGE(TAG, "%s failed: range=%u value=0x%06" PRIx32 " sdk_code=0x%02x",
             call,
             static_cast<unsigned>(error.GetRange()),
             static_cast<uint32_t>(error.GetValue()),
             static_cast<unsigned>(error.GetSdkCode()));
}

static bool partition_already_logged(const char *partition, const char *const *logged, const size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(partition, logged[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void log_nvs_partition_stats(const char *partition) {
    nvs_stats_t stats = {};
    const esp_err_t err = nvs_get_stats(partition, &stats);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS partition '%s' stats unavailable: %s (0x%x)",
                 partition, esp_err_to_name(err), err);
        return;
    }

    ESP_LOGI(TAG,
             "NVS partition '%s': used=%u free=%u available=%u total=%u namespaces=%u",
             partition,
             static_cast<unsigned>(stats.used_entries),
             static_cast<unsigned>(stats.free_entries),
             static_cast<unsigned>(stats.available_entries),
             static_cast<unsigned>(stats.total_entries),
             static_cast<unsigned>(stats.namespace_count));
}

static void log_nvs_namespace_status(const char *partition, const char *name) {
    nvs_handle_t handle = 0;
    const esp_err_t err = nvs_open_from_partition(partition, name, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Matter NVS namespace '%s/%s' exists", partition, name);
        nvs_close(handle);
        return;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Matter NVS namespace '%s/%s' missing; CHIP may create it", partition, name);
        return;
    }

    ESP_LOGW(TAG, "Matter NVS namespace '%s/%s' open failed: %s (0x%x)",
             partition, name, esp_err_to_name(err), err);
}

static void log_matter_storage_status(void) {
    const char *logged_partitions[sizeof(kMatterNamespaces) / sizeof(kMatterNamespaces[0])] = {};
    size_t logged_count = 0;

    for (const auto &ns : kMatterNamespaces) {
        if (!partition_already_logged(ns.partition, logged_partitions, logged_count)) {
            log_nvs_partition_stats(ns.partition);
            logged_partitions[logged_count++] = ns.partition;
        }
    }

    for (const auto &ns : kMatterNamespaces) {
        log_nvs_namespace_status(ns.partition, ns.name);
    }
}

static esp_err_t erase_matter_namespace(const char *partition, const char *name) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open_from_partition(partition, name, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Matter NVS namespace '%s/%s' already absent", partition, name);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open Matter NVS namespace '%s/%s' for reset: %s (0x%x)",
                 partition, name, esp_err_to_name(err), err);
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Erased Matter NVS namespace '%s/%s'", partition, name);
    } else {
        ESP_LOGE(TAG, "Failed to erase Matter NVS namespace '%s/%s': %s (0x%x)",
                 partition, name, esp_err_to_name(err), err);
    }
    return err;
}

esp_err_t matter_interface_init(const esp_matter::event_callback_t handle_chip_device_event,
                                const intptr_t callback_arg) {
    ESP_LOGI(TAG, "Initializing Matter interface");
    set_last_error(nullptr);
    log_heap("before Matter init");
    log_matter_storage_status();

    if (esp_matter_started) {
        ESP_LOGE(TAG, "Matter stack already initialized");
        set_last_error("Matter stack already initialized");
        log_heap("after Matter init failure");
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize OTA requestor (required before Matter stack start)
    ESP_LOGI(TAG, "Calling esp_matter_ota_requestor_init()");
    esp_matter_ota_requestor_init();

    // Initialize CHIP memory
    ESP_LOGI(TAG, "Calling chip::Platform::MemoryInit()");
    CHIP_ERROR chip_error = chip::Platform::MemoryInit();
    if (chip_error != CHIP_NO_ERROR) {
        log_chip_error("chip::Platform::MemoryInit()", chip_error);
        log_heap("after Matter init failure");
        return ESP_ERR_NO_MEM;
    }
    log_heap("after CHIP memory init");

    // Initialize CHIP stack. This step changes Wi-Fi mode to STA
    ESP_LOGI(TAG, "Calling chip::DeviceLayer::PlatformMgr().InitChipStack()");
    chip_error = chip::DeviceLayer::PlatformMgr().InitChipStack();
    if (chip_error != CHIP_NO_ERROR) {
        log_chip_error("chip::DeviceLayer::PlatformMgr().InitChipStack()", chip_error);
        log_matter_storage_status();
        log_heap("after Matter init failure");
        chip::Platform::MemoryShutdown();
        return ESP_FAIL;
    }
    log_heap("after CHIP stack init");

    // Set up default Matter providers (e.g., device info, configuration)
    ESP_LOGI(TAG, "Calling esp_matter::setup_providers()");
    esp_matter::setup_providers();

    // Start Matter platform event loop
    ESP_LOGI(TAG, "Calling chip::DeviceLayer::PlatformMgr().StartEventLoopTask()");
    chip_error = chip::DeviceLayer::PlatformMgr().StartEventLoopTask();
    if (chip_error != CHIP_NO_ERROR) {
        log_chip_error("chip::DeviceLayer::PlatformMgr().StartEventLoopTask()", chip_error);
        chip::Platform::MemoryShutdown();
        log_heap("after Matter init failure");
        return ESP_FAIL;
    }

    // Register the event handler
    ESP_LOGI(TAG, "Registering internal and optional external event handlers");
    chip_error = chip::DeviceLayer::PlatformMgr().AddEventHandler(handle_chip_device_event, callback_arg);
    if (chip_error != CHIP_NO_ERROR) {
        log_chip_error("chip::DeviceLayer::PlatformMgr().AddEventHandler()", chip_error);
        chip::Platform::MemoryShutdown();
        log_heap("after Matter init failure");
        return ESP_FAIL;
    }

    esp_matter_started = true;
    log_matter_storage_status();
    log_heap("after Matter init success");

    return ESP_OK;
}

esp_err_t matter_interface_platform_reset(void) {
    if (esp_matter_started) {
        set_last_error("Matter platform reset requested after Matter stack start");
        ESP_LOGW(TAG, "Refusing Matter platform reset while Matter stack is running");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Erasing Matter-only NVS namespaces; Wi-Fi/WSS data is not erased");
    esp_err_t first_error = ESP_OK;
    for (const auto &ns : kMatterNamespaces) {
        const esp_err_t err = erase_matter_namespace(ns.partition, ns.name);
        if (first_error == ESP_OK && err != ESP_OK) {
            first_error = err;
        }
    }

    log_matter_storage_status();
    if (first_error == ESP_OK) {
        set_last_error("Matter platform namespaces reset; reboot required before Matter init retry");
    }
    return first_error;
}

const char *matter_interface_get_last_error(void) {
    return last_matter_error;
}
