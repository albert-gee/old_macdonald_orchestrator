#include "../../include/messages/json_outbound_message.h"
#include "websocket_server.h"

#include <esp_log.h>
#include <esp_err.h>
#include <cJSON.h>
#include <cstdio>
#include <cstring>

static const char *TAG = "JSON_OUTBOUND";

static esp_err_t create_message(const char *type, const char *action, cJSON *payload) {
    if (!type || !payload) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddStringToObject(root, "type", type);

    if (strcmp(type, "info") == 0 && action) {
        cJSON_AddStringToObject(root, "action", action);
    }

    cJSON_AddItemToObject(root, "payload", payload);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        ESP_LOGE(TAG, "Failed to generate JSON message");
        return ESP_FAIL;
    }

    esp_err_t err = websocket_broadcast_message(json_str);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to broadcast message: %s", esp_err_to_name(err));
    }

    free(json_str);
    return err;
}

// ---- WI-FI

esp_err_t create_info_wifi_status_message(const char *status) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddStringToObject(payload, "status", status);
    return create_message("info", "wifi.sta_status", payload);
}

// ---- THREAD

esp_err_t create_info_thread_status_message(const char *status) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddStringToObject(payload, "status", status);
    return create_message("info", "thread.status", payload);
}


esp_err_t create_info_thread_role_message(const char *role) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddStringToObject(payload, "role", role);
    return create_message("info", "thread.role", payload);
}


static void binary_to_hex_string(const uint8_t *bin, const size_t bin_len, char *hex_str, const size_t hex_size) {
    if (!bin || !hex_str || hex_size < (bin_len * 2 + 1)) {
        ESP_LOGE(TAG, "Invalid arguments in binary_to_hex_string");
        return;
    }

    for (size_t i = 0; i < bin_len; i++) {
        snprintf(hex_str + (i * 2), 3, "%02X", bin[i]);
    }

    hex_str[bin_len * 2] = '\0';
}

esp_err_t create_info_thread_dataset_active_message(uint64_t active_timestamp, const char *network_name,
                                                    const uint8_t *extended_pan_id, const uint8_t *mesh_local_prefix,
                                                    uint16_t pan_id, uint16_t channel) {
    if (!network_name || !extended_pan_id || !mesh_local_prefix) {
        ESP_LOGE(TAG, "Invalid dataset values");
        return ESP_FAIL;
    }

    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    char extended_pan_id_hex[17] = {};
    char mesh_local_prefix_hex[33] = {}; // 16 bytes = 32 hex chars + null

    binary_to_hex_string(extended_pan_id, 8, extended_pan_id_hex, sizeof(extended_pan_id_hex));
    binary_to_hex_string(mesh_local_prefix, 8, mesh_local_prefix_hex, sizeof(mesh_local_prefix_hex));

    cJSON_AddNumberToObject(payload, "active_timestamp", active_timestamp);
    cJSON_AddStringToObject(payload, "network_name", network_name);
    cJSON_AddStringToObject(payload, "extended_pan_id", extended_pan_id_hex);
    cJSON_AddStringToObject(payload, "mesh_local_prefix", mesh_local_prefix_hex);
    cJSON_AddNumberToObject(payload, "pan_id", pan_id);
    cJSON_AddNumberToObject(payload, "channel", channel);

    return create_message("info", "thread.dataset_active", payload);
}

// ---- MATTER ----

esp_err_t create_info_matter_commissioning_complete_message(uint64_t nodeId, uint8_t fabricIndex) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddNumberToObject(payload, "node_id", nodeId);
    cJSON_AddNumberToObject(payload, "fabric_index", fabricIndex);

    return create_message("info", "matter.commissioning_complete", payload);
}


esp_err_t create_info_matter_attribute_report_message(
    uint64_t nodeId,
    uint16_t endpointId,
    uint32_t clusterId,
    uint32_t attributeId,
    char *value
) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddNumberToObject(payload, "node_id", nodeId);
    cJSON_AddNumberToObject(payload, "endpoint_id", endpointId);
    cJSON_AddNumberToObject(payload, "cluster_id", clusterId);
    cJSON_AddNumberToObject(payload, "attribute_id", attributeId);
    cJSON_AddStringToObject(payload, "value", value);

    return create_message("info", "matter.attribute_report", payload);
}

esp_err_t create_info_matter_subscribe_done_message(const uint64_t nodeId, const uint32_t subscription_id) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddNumberToObject(payload, "node_id", nodeId);
    cJSON_AddNumberToObject(payload, "subscription_id", subscription_id);

    return create_message("info", "matter.subscribe_done", payload);
}
