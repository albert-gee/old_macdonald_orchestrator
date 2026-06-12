#include "thread_util.h"

#include <esp_check.h>
#include <esp_log.h>
#include <esp_openthread.h>
#include <esp_openthread_border_router.h>
#include <esp_openthread_lock.h>
#include <nvs_flash.h>
#include <portmacro.h>

#include <openthread/dataset.h>
#include <openthread/dataset_ftd.h>
#include <openthread/ip6.h>
#include <openthread/instance.h>
#include <openthread/thread.h>
#include <cstring>

static const char *TAG = "THREAD_UTIL";

// -----------------------------------------------------------------------------
// Interface Control
// -----------------------------------------------------------------------------

esp_err_t ifconfig_up() {
    esp_openthread_lock_acquire(portMAX_DELAY);

    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "OpenThread instance is NULL");
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = (otIp6SetEnabled(instance, true) == OT_ERROR_NONE) ? ESP_OK : ESP_FAIL;
    esp_openthread_lock_release();

    return err;
}

esp_err_t ifconfig_down() {
    esp_openthread_lock_acquire(portMAX_DELAY);

    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "OpenThread instance is NULL");
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = (otIp6SetEnabled(instance, false) == OT_ERROR_NONE) ? ESP_OK : ESP_FAIL;
    esp_openthread_lock_release();

    return err;
}

esp_err_t thread_get_interface_name(char *interface_name) {
    if (!interface_name) return ESP_ERR_INVALID_ARG;

    strcpy(interface_name, "OPENTHREAD");
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Dataset Configuration
// -----------------------------------------------------------------------------

static int hex_char_to_int(char hex) {
    if ('0' <= hex && hex <= '9') return hex - '0';
    if ('a' <= hex && hex <= 'f') return hex - 'a' + 10;
    if ('A' <= hex && hex <= 'F') return hex - 'A' + 10;
    return -1;
}

static size_t hex_string_to_bytes(const char *hex_string, uint8_t *output_buffer, const size_t output_size) {
    if (!hex_string || !output_buffer) return 0;

    if (size_t hex_len = strlen(hex_string); hex_len != output_size * 2) return 0;

    for (size_t i = 0; i < output_size; ++i) {
        const int high = hex_char_to_int(hex_string[i * 2]);
        const int low  = hex_char_to_int(hex_string[i * 2 + 1]);
        if (high < 0 || low < 0) return 0;
        output_buffer[i] = static_cast<uint8_t>((high << 4) | low);
    }

    return output_size;
}

static bool parse_mesh_local_prefix(const char *mesh_local_prefix, otMeshLocalPrefix *output) {
    if (!mesh_local_prefix || !output) return false;

    otIp6Prefix prefix = {};
    if (otIp6PrefixFromString(mesh_local_prefix, &prefix) == OT_ERROR_NONE) {
        if (prefix.mLength != OT_IP6_PREFIX_BITSIZE) {
            ESP_LOGE(TAG, "Mesh-local prefix must be /%d, got /%u", OT_IP6_PREFIX_BITSIZE, prefix.mLength);
            return false;
        }
        ESP_LOGI(TAG, "Using mesh-local prefix %s", mesh_local_prefix);
        memcpy(output->m8, prefix.mPrefix.mFields.m8, sizeof(output->m8));
        return true;
    }

    otIp6Address address = {};
    if (otIp6AddressFromString(mesh_local_prefix, &address) == OT_ERROR_NONE) {
        ESP_LOGI(TAG, "Using mesh-local prefix %s/64", mesh_local_prefix);
        memcpy(output->m8, address.mFields.m8, sizeof(output->m8));
        return true;
    }

    ESP_LOGE(TAG, "Invalid mesh-local prefix: %s", mesh_local_prefix);
    return false;
}

static esp_err_t ensure_ot_nvs_capacity() {
    nvs_stats_t stats = {};
    const esp_err_t err = nvs_get_stats("ot_nvs", &stats);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot read ot_nvs stats: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "ot_nvs before dataset save: used=%u free=%u total=%u namespaces=%u",
             static_cast<unsigned>(stats.used_entries),
             static_cast<unsigned>(stats.free_entries),
             static_cast<unsigned>(stats.total_entries),
             static_cast<unsigned>(stats.namespace_count));

    if (stats.free_entries < 128) {
        ESP_LOGE(TAG, "ot_nvs has too few free entries for OpenThread dataset save");
        return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    }

    return ESP_OK;
}

static void log_ot_nvs_after_dataset_save() {
    nvs_stats_t stats = {};
    const esp_err_t err = nvs_get_stats("ot_nvs", &stats);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot read ot_nvs stats after dataset save: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "ot_nvs after dataset save: used=%u free=%u total=%u namespaces=%u",
             static_cast<unsigned>(stats.used_entries),
             static_cast<unsigned>(stats.free_entries),
             static_cast<unsigned>(stats.total_entries),
             static_cast<unsigned>(stats.namespace_count));
}

esp_err_t thread_dataset_init(const uint16_t channel, const uint16_t pan_id, const char *network_name,
                              const char *extended_pan_id, const char *mesh_local_prefix,
                              const char *network_key, const char *pskc) {
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) return ESP_ERR_INVALID_STATE;

    if (!esp_openthread_lock_acquire(pdMS_TO_TICKS(5000))) return ESP_FAIL;

    auto *dataset = static_cast<otOperationalDataset *>(calloc(1, sizeof(otOperationalDataset)));
    if (!dataset) {
        esp_openthread_lock_release();
        return ESP_ERR_NO_MEM;
    }

    dataset->mActiveTimestamp.mSeconds = 1;
    dataset->mComponents.mIsActiveTimestampPresent = true;

    dataset->mChannel = channel;
    dataset->mComponents.mIsChannelPresent = true;

    dataset->mPanId = pan_id;
    dataset->mComponents.mIsPanIdPresent = true;

    if (otNetworkNameFromString(&dataset->mNetworkName, network_name) != OT_ERROR_NONE) {
        free(dataset);
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_ARG;
    }
    dataset->mComponents.mIsNetworkNamePresent = true;

    if (hex_string_to_bytes(extended_pan_id, dataset->mExtendedPanId.m8, sizeof(dataset->mExtendedPanId.m8)) != sizeof(dataset->mExtendedPanId.m8)) {
        free(dataset);
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_ARG;
    }
    dataset->mComponents.mIsExtendedPanIdPresent = true;

    if (!parse_mesh_local_prefix(mesh_local_prefix, &dataset->mMeshLocalPrefix)) {
        free(dataset);
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_ARG;
    }
    dataset->mComponents.mIsMeshLocalPrefixPresent = true;

    if (hex_string_to_bytes(network_key, dataset->mNetworkKey.m8, sizeof(dataset->mNetworkKey.m8)) != sizeof(dataset->mNetworkKey.m8)) {
        free(dataset);
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_ARG;
    }
    dataset->mComponents.mIsNetworkKeyPresent = true;

    if (hex_string_to_bytes(pskc, dataset->mPskc.m8, sizeof(dataset->mPskc.m8)) != sizeof(dataset->mPskc.m8)) {
        free(dataset);
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_ARG;
    }
    dataset->mComponents.mIsPskcPresent = true;

    esp_err_t result = ensure_ot_nvs_capacity();
    if (result == ESP_OK) {
        const otError ot_err = otDatasetSetActive(instance, dataset);
        if (ot_err == OT_ERROR_NONE) {
            result = ESP_OK;
        } else if (ot_err == OT_ERROR_NO_BUFS) {
            ESP_LOGE(TAG, "otDatasetSetActive failed with OT_ERROR_NO_BUFS");
            result = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
        } else {
            ESP_LOGE(TAG, "otDatasetSetActive failed: %d", static_cast<int>(ot_err));
            result = ESP_FAIL;
        }
        log_ot_nvs_after_dataset_save();
    }

    free(dataset);
    esp_openthread_lock_release();
    return result;
}

// -----------------------------------------------------------------------------
// Stack Control
// -----------------------------------------------------------------------------

esp_err_t thread_start() {
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) return ESP_ERR_INVALID_STATE;

    esp_openthread_lock_acquire(portMAX_DELAY);
    const bool success = (otThreadSetEnabled(instance, true) == OT_ERROR_NONE);
    esp_openthread_lock_release();

    return success ? ESP_OK : ESP_FAIL;
}

esp_err_t thread_stop() {
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) return ESP_ERR_INVALID_STATE;

    esp_openthread_lock_acquire(portMAX_DELAY);
    const bool success = (otThreadSetEnabled(instance, false) == OT_ERROR_NONE);
    esp_openthread_lock_release();

    return success ? ESP_OK : ESP_FAIL;
}

// -----------------------------------------------------------------------------
// Query Functions
// -----------------------------------------------------------------------------

esp_err_t thread_is_stack_running(bool *is_running) {
    if (!is_running) return ESP_ERR_INVALID_ARG;

    otInstance *instance = esp_openthread_get_instance();
    if (!instance) return ESP_ERR_INVALID_STATE;

    *is_running = (otThreadGetDeviceRole(instance) != OT_DEVICE_ROLE_DISABLED);
    return ESP_OK;
}

esp_err_t thread_is_attached(bool *is_attached) {
    if (!is_attached) return ESP_ERR_INVALID_ARG;

    otInstance *instance = esp_openthread_get_instance();
    if (!instance) return ESP_ERR_INVALID_STATE;

    otDeviceRole role = otThreadGetDeviceRole(instance);
    *is_attached = (role != OT_DEVICE_ROLE_DISABLED && role != OT_DEVICE_ROLE_DETACHED);
    return ESP_OK;
}

esp_err_t thread_get_device_role_string(const char **role_str) {
    otInstance *instance = esp_openthread_get_instance();
    if (!instance || !role_str) return ESP_ERR_INVALID_ARG;

    *role_str = otThreadDeviceRoleToString(otThreadGetDeviceRole(instance));
    return ESP_OK;
}

esp_err_t thread_get_unicast_addresses(char **out_addresses, size_t max, size_t *out_count) {
    otInstance *instance = esp_openthread_get_instance();
    if (!instance || !out_addresses || !out_count) return ESP_ERR_INVALID_ARG;

    const otNetifAddress *addr = otIp6GetUnicastAddresses(instance);
    size_t count = 0;

    while (addr && count < max) {
        char temp[40] = {};
        otIp6AddressToString(&addr->mAddress, temp, sizeof(temp));
        out_addresses[count] = strdup(temp);
        if (!out_addresses[count]) return ESP_ERR_NO_MEM;
        addr = addr->mNext;
        count++;
    }

    *out_count = count;
    return ESP_OK;
}

esp_err_t thread_get_multicast_addresses(char **out_addresses, size_t max, size_t *out_count) {
    otInstance *instance = esp_openthread_get_instance();
    if (!instance || !out_addresses || !out_count) return ESP_ERR_INVALID_ARG;

    const otNetifMulticastAddress *addr = otIp6GetMulticastAddresses(instance);
    size_t count = 0;

    while (addr && count < max) {
        char temp[40] = {};
        otIp6AddressToString(&addr->mAddress, temp, sizeof(temp));
        out_addresses[count] = strdup(temp);
        if (!out_addresses[count]) return ESP_ERR_NO_MEM;
        addr = addr->mNext;
        count++;
    }

    *out_count = count;
    return ESP_OK;
}

void thread_free_address_list(char **addresses, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        free(addresses[i]);
    }
}

esp_err_t thread_get_active_dataset(otOperationalDataset *dataset) {
    otInstance *instance = esp_openthread_get_instance();
    if (!instance || !dataset) return ESP_ERR_INVALID_ARG;

    esp_openthread_lock_acquire(portMAX_DELAY);
    bool success = (otDatasetGetActive(instance, dataset) == OT_ERROR_NONE);
    esp_openthread_lock_release();

    return success ? ESP_OK : ESP_FAIL;
}

esp_err_t thread_get_active_dataset_tlvs(uint8_t *dataset_tlvs, uint8_t *dataset_len) {
    if (!dataset_tlvs || !dataset_len) return ESP_ERR_INVALID_ARG;

    otInstance *instance = esp_openthread_get_instance();
    if (!instance) return ESP_ERR_INVALID_STATE;

    esp_openthread_lock_acquire(portMAX_DELAY);

    otOperationalDatasetTlvs tlvs = {};
    if (otDatasetGetActiveTlvs(instance, &tlvs) != OT_ERROR_NONE) {
        esp_openthread_lock_release();
        return ESP_FAIL;
    }

    if (tlvs.mLength > *dataset_len) {
        esp_openthread_lock_release();
        return ESP_ERR_NO_MEM;
    }

    memcpy(dataset_tlvs, tlvs.mTlvs, tlvs.mLength);
    *dataset_len = tlvs.mLength;

    esp_openthread_lock_release();
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Border Router
// -----------------------------------------------------------------------------

esp_err_t thread_br_init() {
    esp_openthread_set_backbone_netif(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));

    esp_openthread_lock_acquire(portMAX_DELAY);
    const esp_err_t err = esp_openthread_border_router_init();
    esp_openthread_lock_release();

    return err;
}

esp_err_t thread_br_deinit(void) {
    esp_openthread_lock_acquire(portMAX_DELAY);
    esp_err_t err = esp_openthread_border_router_deinit();
    esp_openthread_lock_release();
    return err;
}
