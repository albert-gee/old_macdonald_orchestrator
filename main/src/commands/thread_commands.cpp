#include "commands/thread_commands.h"
#include "thread_util.h"
#include <esp_log.h>
#include <esp_check.h>
#include <cJSON.h>
#include <cstring>

static const char *TAG = "THREAD_COMMANDS";

// ---- Stack Control ----

esp_err_t execute_thread_enable_command() {
    ESP_RETURN_ON_ERROR(ifconfig_up(), TAG, "Failed to bring interface up");
    ESP_RETURN_ON_ERROR(thread_start(), TAG, "Failed to start Thread stack");
    return ESP_OK;
}

esp_err_t execute_thread_disable_command() {
    ESP_RETURN_ON_ERROR(thread_stop(), TAG, "Failed to stop Thread stack");
    ESP_RETURN_ON_ERROR(ifconfig_down(), TAG, "Failed to bring interface down");
    return ESP_OK;
}

// ---- Dataset ----

esp_err_t execute_thread_dataset_init_command(uint16_t channel, uint16_t pan_id, const char *network_name,
                                              const char *extended_pan_id, const char *mesh_local_prefix,
                                              const char *network_key, const char *pskc) {
    if (!network_name || !extended_pan_id || !mesh_local_prefix || !network_key || !pskc) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    return thread_dataset_init(channel, pan_id, network_name, extended_pan_id,
                               mesh_local_prefix, network_key, pskc);
}

// ---- Status / Monitoring ----

esp_err_t execute_thread_status_get_command(bool *is_running) {
    return thread_is_stack_running(is_running);
}

esp_err_t execute_thread_attached_get_command(bool *is_attached) {
    return thread_is_attached(is_attached);
}

esp_err_t execute_thread_role_get_command(const char **role_str) {
    return thread_get_device_role_string(role_str);
}

esp_err_t execute_thread_active_dataset_get_command(char *json_buf, size_t buf_size) {
    otOperationalDataset dataset;
    if (thread_get_active_dataset(&dataset) != ESP_OK) {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    char ext_pan_id[17] = {};
    char mesh_prefix[33] = {};

    for (int i = 0; i < 8; ++i) {
        snprintf(ext_pan_id + i * 2, 3, "%02X", dataset.mExtendedPanId.m8[i]);
        snprintf(mesh_prefix + i * 4, 5, "%02X00", dataset.mMeshLocalPrefix.m8[i]); // simplified
    }

    cJSON_AddStringToObject(root, "network_name", (const char *)dataset.mNetworkName.m8);
    cJSON_AddNumberToObject(root, "channel", dataset.mChannel);
    cJSON_AddNumberToObject(root, "pan_id", dataset.mPanId);
    cJSON_AddStringToObject(root, "extended_pan_id", ext_pan_id);
    cJSON_AddStringToObject(root, "mesh_local_prefix", mesh_prefix);

    char *json = cJSON_PrintUnformatted(root);
    if (!json) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy(json_buf, json, buf_size - 1);
    json_buf[buf_size - 1] = '\\0';

    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t execute_thread_unicast_addresses_get_command(char **addresses, size_t max, size_t *count) {
    return thread_get_unicast_addresses(addresses, max, count);
}

esp_err_t execute_thread_multicast_addresses_get_command(char **addresses, size_t max, size_t *count) {
    return thread_get_multicast_addresses(addresses, max, count);
}

// ---- Border Router ----

esp_err_t execute_thread_br_init_command() {
    return thread_br_init();
}

esp_err_t execute_thread_br_deinit_command() {
    return thread_br_deinit();
}