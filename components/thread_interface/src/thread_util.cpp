#include "thread_util.h"

#include <openthread/dataset.h>
#include <openthread/dataset_ftd.h>
#include <openthread/instance.h>
#include <openthread/thread.h>
#include <esp_log.h>
#include <esp_check.h>
#include <esp_openthread.h>
#include <esp_openthread_border_router.h>
#include <esp_openthread_lock.h>
#include <portmacro.h>

static const char *TAG = "THREAD_UTIL";

/**
 * @brief Converts a hexadecimal character to its integer value.
 *
 * This function converts a character ('0'-'9', 'A'-'F', 'a'-'f') to its
 * corresponding integer value (0-15). If the character is invalid, it returns -1.
 *
 * @param hex The hexadecimal character to convert.
 * @return The integer value (0-15) or -1 if the input is invalid.
 */
static int hex_char_to_int(const char hex)
{
    if ('A' <= hex && hex <= 'F') {
        return 10 + hex - 'A';
    }
    if ('a' <= hex && hex <= 'f') {
        return 10 + hex - 'a';
    }
    if ('0' <= hex && hex <= '9') {
        return hex - '0';
    }
    return -1;
}

/**
 * @brief Converts a hexadecimal string to a byte array.
 *
 * This function converts a string of hexadecimal characters into a byte array.
 * The string must have twice the length of the output buffer. If the string
 * is invalid or the lengths don't match, the function returns 0.
 *
 * @param hex_string The null-terminated hexadecimal string to convert.
 * @param output_buffer The buffer to store the resulting byte array.
 * @param output_size The size of the output buffer in bytes.
 * @return The number of bytes written to the output buffer, or 0 if the conversion fails.
 */
static size_t hex_string_to_bytes(const char *hex_string, uint8_t *output_buffer, const size_t output_size)
{
    const size_t hex_string_length = strlen(hex_string);

    if (hex_string_length != output_size * 2) {
        return 0;
    }

    for (size_t i = 0; i < hex_string_length; i += 2) {
        const int high_nibble = hex_char_to_int(hex_string[i]);
        const int low_nibble = hex_char_to_int(hex_string[i + 1]);

        if (high_nibble < 0 || low_nibble < 0) {
            return 0;
        }

        output_buffer[i / 2] = (high_nibble << 4) + low_nibble;
    }

    return output_size;
}


esp_err_t thread_dataset_init_new() {
    ESP_LOGI(TAG, "Initializing new OpenThread dataset...");

    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "OpenThread instance is NULL");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Retrieved OpenThread instance: %p", (void *)instance);

    if (!esp_openthread_lock_acquire(pdMS_TO_TICKS(5000))) {
        ESP_LOGE(TAG, "Failed to acquire OpenThread lock within timeout.");
        return ESP_FAIL;
    }

    auto *dataset = static_cast<otOperationalDataset *>(malloc(sizeof(otOperationalDataset)));
    if (!dataset) {
        ESP_LOGE(TAG, "Failed to allocate memory for dataset");
        esp_openthread_lock_release();
        return ESP_ERR_NO_MEM;
    }
    memset(dataset, 0, sizeof(otOperationalDataset));

    dataset->mActiveTimestamp.mSeconds = 1;
    dataset->mActiveTimestamp.mTicks = 0;
    dataset->mActiveTimestamp.mAuthoritative = false;
    dataset->mComponents.mIsActiveTimestampPresent = true;

    dataset->mChannel = CONFIG_OPENTHREAD_NETWORK_CHANNEL;
    dataset->mComponents.mIsChannelPresent = true;

    dataset->mPanId = CONFIG_OPENTHREAD_NETWORK_PANID;
    dataset->mComponents.mIsPanIdPresent = true;

    esp_err_t err = otNetworkNameFromString(&dataset->mNetworkName, CONFIG_OPENTHREAD_NETWORK_NAME);
    if (err != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to set Network Name: %s", esp_err_to_name(err));
        free(dataset);
        esp_openthread_lock_release();
        return ESP_FAIL;
    }
    dataset->mComponents.mIsNetworkNamePresent = true;

    size_t len = hex_string_to_bytes(CONFIG_OPENTHREAD_NETWORK_EXTPANID, dataset->mExtendedPanId.m8, sizeof(dataset->mExtendedPanId.m8));
    if (len != sizeof(dataset->mExtendedPanId.m8)) {
        ESP_LOGI(TAG, "Failed to set Extended PAN ID.");
        free(dataset);
        esp_openthread_lock_release();
        return ESP_FAIL;
    }
    dataset->mComponents.mIsExtendedPanIdPresent = true;

    otIp6Prefix prefix = {};
    if (otIp6PrefixFromString(CONFIG_OPENTHREAD_MESH_LOCAL_PREFIX, &prefix) != OT_ERROR_NONE) {
        ESP_LOGI(TAG, "Failed to parse Mesh Local Prefix: %s", CONFIG_OPENTHREAD_MESH_LOCAL_PREFIX);
        free(dataset);
        esp_openthread_lock_release();
        return ESP_FAIL;
    }
    memcpy(dataset->mMeshLocalPrefix.m8, prefix.mPrefix.mFields.m8, sizeof(dataset->mMeshLocalPrefix.m8));
    dataset->mComponents.mIsMeshLocalPrefixPresent = true;

    len = hex_string_to_bytes(CONFIG_OPENTHREAD_NETWORK_MASTERKEY, dataset->mNetworkKey.m8, sizeof(dataset->mNetworkKey.m8));
    if (len != sizeof(dataset->mNetworkKey.m8)) {
        ESP_LOGE(TAG, "Failed to parse OpenThread network key: %s", esp_err_to_name(err));
        free(dataset);
        esp_openthread_lock_release();
        return ESP_FAIL;
    }
    dataset->mComponents.mIsNetworkKeyPresent = true;

    len = hex_string_to_bytes(CONFIG_OPENTHREAD_NETWORK_PSKC, dataset->mPskc.m8, sizeof(dataset->mPskc.m8));
    if (len != sizeof(dataset->mPskc.m8)) {
        ESP_LOGI(TAG, "Failed to set PSKc.");
        free(dataset);
        esp_openthread_lock_release();
        return ESP_FAIL;
    }
    dataset->mComponents.mIsPskcPresent = true;

    err = otDatasetSetActive(instance, dataset);
    if (err != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to set OpenThread active dataset: %s", esp_err_to_name(err));
        free(dataset);
        esp_openthread_lock_release();
        return ESP_FAIL;
    }

    free(dataset);
    esp_openthread_lock_release();
    ESP_LOGI(TAG, "OpenThread dataset initialization completed successfully.");
    return ESP_OK;
}

esp_err_t thread_dataset_init(const uint16_t channel, const uint16_t pan_id, const char *network_name,
                              const char *extended_pan_id, const char *mesh_local_prefix,
                              const char *network_key, const char *pskc)
{
    ESP_LOGI(TAG, "Initializing new OpenThread dataset...");

    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "OpenThread instance is NULL");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Retrieved OpenThread instance: %p", (void *)instance);

    if (!esp_openthread_lock_acquire(pdMS_TO_TICKS(5000))) {
        ESP_LOGE(TAG, "Failed to acquire OpenThread lock within timeout.");
        return ESP_FAIL;
    }

    auto *dataset = static_cast<otOperationalDataset *>(malloc(sizeof(otOperationalDataset)));
    if (!dataset) {
        ESP_LOGE(TAG, "Failed to allocate memory for dataset");
        esp_openthread_lock_release();
        return ESP_ERR_NO_MEM;
    }
    memset(dataset, 0, sizeof(otOperationalDataset));

    dataset->mActiveTimestamp.mSeconds = 1;
    dataset->mActiveTimestamp.mTicks = 0;
    dataset->mActiveTimestamp.mAuthoritative = false;
    dataset->mComponents.mIsActiveTimestampPresent = true;

    dataset->mChannel = channel;
    dataset->mComponents.mIsChannelPresent = true;

    dataset->mPanId = pan_id;
    dataset->mComponents.mIsPanIdPresent = true;

    esp_err_t err = otNetworkNameFromString(&dataset->mNetworkName, network_name);
    if (err != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to set Network Name: %s", esp_err_to_name(err));
        free(dataset);
        esp_openthread_lock_release();
        return ESP_FAIL;
    }
    dataset->mComponents.mIsNetworkNamePresent = true;

    size_t len = hex_string_to_bytes(extended_pan_id, dataset->mExtendedPanId.m8, sizeof(dataset->mExtendedPanId.m8));
    if (len != sizeof(dataset->mExtendedPanId.m8)) {
        ESP_LOGI(TAG, "Failed to set Extended PAN ID.");
        free(dataset);
        esp_openthread_lock_release();
        return ESP_FAIL;
    }
    dataset->mComponents.mIsExtendedPanIdPresent = true;

    otIp6Prefix prefix = {};
    if (otIp6PrefixFromString(mesh_local_prefix, &prefix) != OT_ERROR_NONE) {
        ESP_LOGI(TAG, "Failed to parse Mesh Local Prefix: %s", mesh_local_prefix);
        free(dataset);
        esp_openthread_lock_release();
        return ESP_FAIL;
    }
    memcpy(dataset->mMeshLocalPrefix.m8, prefix.mPrefix.mFields.m8, sizeof(dataset->mMeshLocalPrefix.m8));
    dataset->mComponents.mIsMeshLocalPrefixPresent = true;

    len = hex_string_to_bytes(network_key, dataset->mNetworkKey.m8, sizeof(dataset->mNetworkKey.m8));
    if (len != sizeof(dataset->mNetworkKey.m8)) {
        ESP_LOGE(TAG, "Failed to parse OpenThread network key: %s", esp_err_to_name(err));
        free(dataset);
        esp_openthread_lock_release();
        return ESP_FAIL;
    }
    dataset->mComponents.mIsNetworkKeyPresent = true;

    len = hex_string_to_bytes(pskc, dataset->mPskc.m8, sizeof(dataset->mPskc.m8));
    if (len != sizeof(dataset->mPskc.m8)) {
        ESP_LOGI(TAG, "Failed to set PSKc.");
        free(dataset);
        esp_openthread_lock_release();
        return ESP_FAIL;
    }
    dataset->mComponents.mIsPskcPresent = true;

    err = otDatasetSetActive(instance, dataset);
    if (err != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to set OpenThread active dataset: %s", esp_err_to_name(err));
        free(dataset);
        esp_openthread_lock_release();
        return ESP_FAIL;
    }

    free(dataset);
    esp_openthread_lock_release();
    ESP_LOGI(TAG, "OpenThread dataset initialization completed successfully.");
    return ESP_OK;
}

esp_err_t ifconfig_up() {
    esp_openthread_lock_acquire(portMAX_DELAY);


    ESP_LOGI(TAG, "Entering ifconfig_up");


    otInstance *openThreadInstance = esp_openthread_get_instance();
    if (!openThreadInstance) {
        ESP_LOGE(TAG, "OpenThread instance is NULL");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "OpenThread instance is valid");

    if (otIp6SetEnabled(openThreadInstance, true) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to enable OpenThread IPv6 interface");
        esp_openthread_lock_release(); // Release the lock before returning
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Successfully enabled OpenThread IPv6 interface");

    esp_openthread_lock_release();
    ESP_LOGI(TAG, "Releasing OpenThread lock");

    return ESP_OK;
}

esp_err_t ifconfig_down() {

    esp_openthread_lock_acquire(portMAX_DELAY);

    otInstance *openThreadInstance = esp_openthread_get_instance();
    if (!openThreadInstance) {
        ESP_LOGE(TAG, "OpenThread instance is NULL");
        return ESP_ERR_INVALID_STATE;
    }

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

esp_err_t thread_get_device_role_name(char *device_role_name) {
    otInstance *openThreadInstance = esp_openthread_get_instance();
    if (!openThreadInstance) {
        return ESP_FAIL;
    }

    otDeviceRole role = otThreadGetDeviceRole(openThreadInstance);

    const char *role_str = "Unknown";
    switch (role) {
        case OT_DEVICE_ROLE_DISABLED: role_str = "Disabled"; break;
        case OT_DEVICE_ROLE_DETACHED: role_str = "Detached"; break;
        case OT_DEVICE_ROLE_CHILD: role_str = "Child"; break;
        case OT_DEVICE_ROLE_ROUTER: role_str = "Router"; break;
        case OT_DEVICE_ROLE_LEADER: role_str = "Leader"; break;
    }

    snprintf(device_role_name, 22, "%s", role_str);
    return ESP_OK;
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

esp_err_t thread_br_init() {
    esp_openthread_set_backbone_netif(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));

    esp_err_t err = ESP_OK;

    esp_openthread_lock_acquire(portMAX_DELAY);
    err = esp_openthread_border_router_init();
    esp_openthread_lock_release();

    return err;
}