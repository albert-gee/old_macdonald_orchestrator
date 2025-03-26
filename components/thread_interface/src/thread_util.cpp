#include "thread_util.h"

#include <openthread/dataset.h>
#include <openthread/dataset_ftd.h>
#include <openthread/instance.h>
#include <openthread/thread.h>
#include <esp_log.h>
#include <esp_check.h>
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <portmacro.h>
#include <cstring>

static const char *TAG = "THREAD_UTIL";

static int hex_digit_to_int(unsigned char hex) {
    if (hex >= '0' && hex <= '9') {
        return hex - '0';
    }
    if (hex >= 'A' && hex <= 'F') {
        return hex - 'A' + 10;
    }
    if (hex >= 'a' && hex <= 'f') {
        return hex - 'a' + 10;
    }
    return -1;  // Invalid hex character
}

static size_t hex_string_to_binary(const char *hex_string, uint8_t *buf, size_t buf_size) {
    if (!hex_string || !buf) {
        return 0;
    }

    const size_t num_char = strlen(hex_string);
    if (num_char != buf_size * 2 || num_char % 2 != 0) {
        return 0; // Must be exactly 2 * buf_size characters
    }

    for (size_t i = 0; i < num_char; i += 2) {
        const int digit0 = hex_digit_to_int(static_cast<unsigned char>(hex_string[i]));
        const int digit1 = hex_digit_to_int(static_cast<unsigned char>(hex_string[i + 1]));

        if (digit0 < 0 || digit1 < 0) {
            return 0; // Invalid hex digit found
        }
        buf[i / 2] = static_cast<uint8_t>((digit0 << 4) | digit1);
    }

    return buf_size;
}

esp_err_t thread_dataset_init(const uint16_t channel, const uint16_t pan_id, const char *network_name,
                              const char *extended_pan_id, const char *mesh_local_prefix,
                              const char *master_key, const char *pskc) {
    if (!network_name || !extended_pan_id || !mesh_local_prefix || !master_key || !pskc) {
        ESP_LOGE(TAG, "Invalid input parameters (NULL pointers)");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing new OpenThread dataset");

    otInstance *openThreadInstance = esp_openthread_get_instance();
    ESP_RETURN_ON_ERROR(openThreadInstance ? ESP_OK : ESP_FAIL, TAG, "OpenThread instance is not initialized");

    otOperationalDataset dataset = {0};
    otIp6Prefix prefix;
    otError ot_err;

    // Acquire OpenThread lock
    esp_openthread_lock_acquire(portMAX_DELAY);

    // Set Active Timestamp
    dataset.mActiveTimestamp.mSeconds = 1;
    dataset.mComponents.mIsActiveTimestampPresent = true;

    // Set Channel
    dataset.mChannel = channel;
    dataset.mComponents.mIsChannelPresent = true;

    // Set PAN ID
    dataset.mPanId = pan_id;
    dataset.mComponents.mIsPanIdPresent = true;

    // Set Network Name
    if (const size_t len = strlen(network_name); len == 0 || len >= OT_NETWORK_NAME_MAX_SIZE) {
        ESP_LOGE(TAG, "Invalid network name length: %d", len);
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_ARG;
    }
    memset(dataset.mNetworkName.m8, 0, sizeof(dataset.mNetworkName.m8));
    snprintf(static_cast<char *>(dataset.mNetworkName.m8), sizeof(dataset.mNetworkName.m8), "%s", network_name);
    dataset.mComponents.mIsNetworkNamePresent = true;

    // Set Extended PAN ID
    if (hex_string_to_binary(extended_pan_id, dataset.mExtendedPanId.m8, sizeof(dataset.mExtendedPanId.m8)) != sizeof(
            dataset.mExtendedPanId.m8)) {
        ESP_LOGE(TAG, "Invalid extended PAN ID format");
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_ARG;
    }
    dataset.mComponents.mIsExtendedPanIdPresent = true;

    // Set Mesh Local Prefix
    memset(&prefix, 0, sizeof(otIp6Prefix));
    if ((ot_err = otIp6PrefixFromString(mesh_local_prefix, &prefix)) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to parse mesh local prefix: %s (error: %d)", mesh_local_prefix, ot_err);
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(dataset.mMeshLocalPrefix.m8, prefix.mPrefix.mFields.m8, sizeof(dataset.mMeshLocalPrefix.m8));
    dataset.mComponents.mIsMeshLocalPrefixPresent = true;

    // Set Network Key
    if (hex_string_to_binary(master_key, dataset.mNetworkKey.m8, sizeof(dataset.mNetworkKey.m8)) != sizeof(dataset.
            mNetworkKey.m8)) {
        ESP_LOGE(TAG, "Invalid master key format");
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_ARG;
    }
    dataset.mComponents.mIsNetworkKeyPresent = true;

    // Set PSKc
    if (hex_string_to_binary(pskc, dataset.mPskc.m8, sizeof(dataset.mPskc.m8)) != sizeof(dataset.mPskc.m8)) {
        ESP_LOGE(TAG, "Invalid PSKc format");
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_ARG;
    }
    dataset.mComponents.mIsPskcPresent = true;

    // Apply dataset
    if ((ot_err = otDatasetSetActive(openThreadInstance, &dataset)) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to set active dataset (error: %d)", ot_err);
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Successfully initialized OpenThread dataset");
    esp_openthread_lock_release();

    return ESP_OK;
}

esp_err_t ifconfig_up() {
    otInstance *openThreadInstance = esp_openthread_get_instance();
    ESP_RETURN_ON_ERROR(openThreadInstance ? ESP_OK : ESP_FAIL, TAG, "OpenThread instance is not initialized");

    esp_openthread_lock_acquire(portMAX_DELAY);

    if (otIp6SetEnabled(openThreadInstance, true) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to enable OpenThread IPv6 interface");
        esp_openthread_lock_release(); // Release the lock before returning
        return ESP_FAIL;
    }

    esp_openthread_lock_release();

    return ESP_OK;
}

esp_err_t ifconfig_down() {
    otInstance *openThreadInstance = esp_openthread_get_instance();
    ESP_RETURN_ON_ERROR(openThreadInstance ? ESP_OK : ESP_FAIL, TAG, "OpenThread instance is not initialized");

    esp_openthread_lock_acquire(portMAX_DELAY);

    if (otIp6SetEnabled(openThreadInstance, false) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to disable OpenThread IPv6 interface");
        esp_openthread_lock_release(); // Release the lock before returning
        return ESP_FAIL;
    }

    esp_openthread_lock_release();

    return ESP_OK;
}

esp_err_t thread_start() {
    otInstance *openThreadInstance = esp_openthread_get_instance();
    ESP_RETURN_ON_ERROR(openThreadInstance ? ESP_OK : ESP_FAIL, TAG, "OpenThread instance is not initialized");

    esp_openthread_lock_acquire(portMAX_DELAY);

    if (otThreadSetEnabled(openThreadInstance, true) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to enable OpenThread");
        esp_openthread_lock_release(); // Release the lock before returning
        return ESP_FAIL;
    }

    esp_openthread_lock_release();

    return ESP_OK;
}

esp_err_t thread_stop() {
    otInstance *openThreadInstance = esp_openthread_get_instance();
    ESP_RETURN_ON_ERROR(openThreadInstance ? ESP_OK : ESP_FAIL, TAG, "OpenThread instance is not initialized");

    esp_openthread_lock_acquire(portMAX_DELAY);

    if (otThreadSetEnabled(openThreadInstance, false) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to disable OpenThread");
        esp_openthread_lock_release(); // Release the lock before returning
        return ESP_FAIL;
    }

    esp_openthread_lock_release();

    return ESP_OK;
}

esp_err_t thread_get_device_role_name(const char *device_role_name) {

    otInstance *openThreadInstance = esp_openthread_get_instance();
    ESP_RETURN_ON_ERROR(openThreadInstance ? ESP_OK : ESP_FAIL, TAG, "OpenThread instance is not initialized");

    otDeviceRole role = otThreadGetDeviceRole(openThreadInstance);
    esp_err_t ret = ESP_OK;

    if (role == OT_DEVICE_ROLE_DISABLED) {
        ESP_LOGI(TAG, "Device Role: Disabled");
        if (device_role_name != NULL) {
            snprintf(const_cast<char *>(device_role_name), strlen("Disabled") + 1, "Disabled");
        }
    } else if (role == OT_DEVICE_ROLE_DETACHED) {
        ESP_LOGI(TAG, "Device Role: Detached");
        if (device_role_name != NULL) {
            snprintf(const_cast<char *>(device_role_name), strlen("Detached") + 1, "Detached");
        }
    } else if (role == OT_DEVICE_ROLE_CHILD) {
        ESP_LOGI(TAG, "Device Role: Child");
        if (device_role_name != NULL) {
            snprintf(const_cast<char *>(device_role_name), strlen("Child") + 1, "Child");
        }
    } else if (role == OT_DEVICE_ROLE_ROUTER) {
        ESP_LOGI(TAG, "Device Role: Router");
        if (device_role_name != NULL) {
            snprintf(const_cast<char *>(device_role_name), strlen("Router") + 1, "Router");
        }
    } else if (role == OT_DEVICE_ROLE_LEADER) {
        ESP_LOGI(TAG, "Device Role: Leader");
        if (device_role_name != NULL) {
            snprintf(const_cast<char *>(device_role_name), strlen("Leader") + 1, "Leader");
        }
    } else {
        ESP_LOGE(TAG, "Unknown device role: %d", role);
        ret = ESP_ERR_INVALID_STATE;
    }

    return ret;
}

esp_err_t thread_get_active_dataset(otOperationalDataset *dataset) {

    otInstance *openThreadInstance = esp_openthread_get_instance();
    ESP_RETURN_ON_ERROR(openThreadInstance ? ESP_OK : ESP_FAIL, TAG, "OpenThread instance is not initialized");

    esp_openthread_lock_acquire(portMAX_DELAY);

    if (otDatasetGetActive(openThreadInstance, dataset) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to get active dataset");
        esp_openthread_lock_release(); // Release the lock before returning
        return ESP_FAIL;
    }

    esp_openthread_lock_release();

    return ESP_OK;
}