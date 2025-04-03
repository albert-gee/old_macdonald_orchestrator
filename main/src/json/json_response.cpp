#include "json/json_response.h"

#include <esp_log.h>
#include <cJSON.h>
#include <cstdio>
#include <cstring>

static const char *TAG = "JSON_RESPONSE";

char *create_json_response(const char *command, const char *payload) {
    if (!command || !payload) {
        ESP_LOGE(TAG, "Invalid arguments: command or payload is NULL");
        return nullptr;
    }

    cJSON *response = cJSON_CreateObject();
    if (!response) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return nullptr;
    }

    cJSON_AddStringToObject(response, "command", command);
    cJSON_AddStringToObject(response, "payload", payload);

    char *response_string = cJSON_PrintUnformatted(response);
    if (!response_string) {
        ESP_LOGE(TAG, "Failed to print JSON object");
    }

    cJSON_Delete(response); // Free JSON object only after ensuring response_string is not NULL
    return response_string;
}

char *create_json_error_response(const char *error_message) {
    return create_json_response("error", error_message);
}

char *create_json_info_response(const char *info_message) {
    return create_json_response("info", info_message);
}

char *create_json_thread_role_set_response(const char *role) {
    return create_json_response("thread_role_set", role);
}

char *create_json_ifconfig_status_response(const char *status) {
    return create_json_response("ifconfig_status", status);
}

char *create_json_thread_status_response(const char *status) {
    return create_json_response("thread_status", status);
}

char *create_json_wifi_sta_status_response(const char *status) {
    return create_json_response("wifi_sta_status", status);
}

/**
 * @brief Converts binary data to a hex string.
 *
 * @param bin        Pointer to the binary data.
 * @param bin_len    Length of the binary data.
 * @param hex_str    Output buffer for the hex string.
 * @param hex_size   Size of the hex buffer (should be at least `bin_len * 2 + 1`).
 */
void binary_to_hex_string(const uint8_t *bin, size_t bin_len, char *hex_str, size_t hex_size) {
    if (!bin || !hex_str || hex_size < (bin_len * 2 + 1)) {
        ESP_LOGE(TAG, "Invalid arguments in binary_to_hex_string");
        return;
    }

    for (size_t i = 0; i < bin_len; i++) {
        snprintf(hex_str + (i * 2), 3, "%02X", bin[i]); // Ensure null termination
    }

    hex_str[bin_len * 2] = '\0'; // Null-terminate the string
}
char *create_json_set_dataset_response(uint64_t active_timestamp, uint64_t pending_timestamp,
                                     const uint8_t *network_key, const char *network_name,
                                     const uint8_t *extended_pan_id, const char *mesh_local_prefix,
                                     uint32_t delay, uint16_t pan_id, uint16_t channel,
                                     uint16_t wakeup_channel, const uint8_t *pskc) {
    if (!network_key || !network_name || !extended_pan_id || !mesh_local_prefix || !pskc) {
        ESP_LOGE(TAG, "Invalid arguments passed to create_json_set_dataset_response");
        return create_json_error_response("Invalid dataset values");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create root JSON object");
        return create_json_error_response("Failed to allocate memory");
    }

    // Add command field
    cJSON_AddStringToObject(root, "command", "thread_dataset_active");

    // Create and populate the payload object
    cJSON *payload = cJSON_CreateObject();
    if (!payload) {
        ESP_LOGE(TAG, "Failed to create payload JSON object");
        cJSON_Delete(root);
        return create_json_error_response("Failed to allocate memory");
    }

    char network_key_hex[33] = {0};
    char extended_pan_id_hex[17] = {0};
    char pskc_hex[33] = {0};

    binary_to_hex_string(network_key, 16, network_key_hex, sizeof(network_key_hex));
    binary_to_hex_string(extended_pan_id, 8, extended_pan_id_hex, sizeof(extended_pan_id_hex));
    binary_to_hex_string(pskc, 16, pskc_hex, sizeof(pskc_hex));

    cJSON_AddNumberToObject(payload, "active_timestamp", active_timestamp);
    cJSON_AddNumberToObject(payload, "pending_timestamp", pending_timestamp);
    cJSON_AddStringToObject(payload, "network_key", network_key_hex);
    cJSON_AddStringToObject(payload, "network_name", network_name);
    cJSON_AddStringToObject(payload, "extended_pan_id", extended_pan_id_hex);
    cJSON_AddStringToObject(payload, "mesh_local_prefix", mesh_local_prefix);
    cJSON_AddNumberToObject(payload, "delay", delay);
    cJSON_AddNumberToObject(payload, "pan_id", pan_id);
    cJSON_AddNumberToObject(payload, "channel", channel);
    cJSON_AddNumberToObject(payload, "wakeup_channel", wakeup_channel);
    cJSON_AddStringToObject(payload, "pskc", pskc_hex);

    // Add the payload object to the root
    cJSON_AddItemToObject(root, "payload", payload);

    char *response_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);  // This will also delete the payload object

    if (!response_string) {
        ESP_LOGE(TAG, "Failed to print JSON object");
        return create_json_error_response("JSON serialization failed");
    }

    return response_string;
}