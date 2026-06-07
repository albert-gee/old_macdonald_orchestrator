#include "messages/outbound_message_builder.h"
#include "control/temperature_control.h"
#include "registry/device_registry.h"
#include "websocket_server.h"

#include <esp_log.h>
#include <esp_err.h>
#include <esp_check.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <inttypes.h>

static const char *TAG = "JSON_OUTBOUND";
static constexpr uint32_t MATTER_REPORT_QUEUE_LENGTH = 8;
static constexpr uint32_t MATTER_REPORT_TASK_STACK_SIZE = 6144;
static constexpr UBaseType_t MATTER_REPORT_TASK_PRIORITY = 5;

struct MatterAttributeReportWork {
    uint64_t node_id;
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t attribute_id;
    char value[256];
};

static QueueHandle_t matter_report_queue = nullptr;
static TaskHandle_t matter_report_task_handle = nullptr;

static esp_err_t ensure_matter_report_worker(void);

esp_err_t outbound_message_builder_init(void) {
    return ensure_matter_report_worker();
}

/**
 * Builds a JSON message string with the given type, action, and payload.
 *
 * @param type The type of the message. This must not be null.
 * @param action Optional action field for the message. This is only included if the type is "info".
 * @param payload The cJSON object representing the payload of the message. This must not be null.
 * @return A pointer to the generated JSON string if successful, or nullptr on failure.
 *         The caller is responsible for freeing the memory allocated for the returned string.
 */
static char *build_json_message(const char *type, const char *action, cJSON *payload) {
    if (!type || !payload) return nullptr;

    cJSON *root = cJSON_CreateObject();
    if (!root) return nullptr;

    cJSON_AddStringToObject(root, "type", type);

    if (strcmp(type, "info") == 0 && action) {
        cJSON_AddStringToObject(root, "action", action);
    } else if (strcmp(type, "event") == 0 && action) {
        cJSON_AddStringToObject(root, "event", action);
    }

    cJSON_AddItemToObject(root, "payload", payload);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json_str; // Caller is responsible for freeing this memory.
}

/**
 * @brief Broadcasts a JSON-formatted message based on the given type, action, and payload.
 *
 * This function constructs a JSON message using the provided parameters and broadcasts it
 * via a WebSocket connection. The caller is responsible for ensuring that the input
 * parameters meet the requirements of the function.
 *
 * @param type The type of the message. This parameter is mandatory and cannot be null.
 * @param action The action of the message. This parameter is optional and can be null.
 * @param payload The JSON payload to attach to the message. This parameter is mandatory and cannot be null.
 * @return
 * - ESP_OK: The message was successfully broadcasted.
 * - ESP_FAIL: JSON message generation failed or broadcasting via WebSocket failed.
 * - Other values: Any specific error codes returned by the `websocket_broadcast_message` function.
 */
static esp_err_t broadcast_message(const char *type, const char *action, cJSON *payload) {
    if (!websocket_server_is_running()) {
        cJSON_Delete(payload);
        return ESP_OK;
    }

    char *json_str = build_json_message(type, action, payload);
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

// ---- THREAD

esp_err_t broadcast_info_thread_stack_status_message(const bool is_running) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddBoolToObject(payload, "running", is_running);
    return broadcast_message("info", "thread.stack_status", payload);
}

esp_err_t broadcast_info_thread_interface_status_message(const bool is_up) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddBoolToObject(payload, "interface_up", is_up);
    return broadcast_message("info", "thread.interface_status", payload);
}

esp_err_t broadcast_info_thread_attachment_status_message(const bool is_attached) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddBoolToObject(payload, "attached", is_attached);
    return broadcast_message("info", "thread.attachment_status", payload);
}

esp_err_t broadcast_info_thread_role_message(const char *role) {
    if (!role) return ESP_ERR_INVALID_ARG;

    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddStringToObject(payload, "role", role);
    return broadcast_message("info", "thread.role", payload);
}

esp_err_t broadcast_info_unicast_addresses_message(const char **addresses, const size_t count) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON *array = cJSON_CreateArray();
    if (!array) {
        cJSON_Delete(payload);
        return ESP_FAIL;
    }

    for (size_t i = 0; i < count; ++i) {
        if (addresses[i]) {
            cJSON_AddItemToArray(array, cJSON_CreateString(addresses[i]));
        }
    }

    cJSON_AddItemToObject(payload, "unicast", array);
    return broadcast_message("info", "ipv6.unicast_addresses", payload);
}

esp_err_t broadcast_info_multicast_addresses_message(const char **addresses, size_t count) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON *array = cJSON_CreateArray();
    if (!array) {
        cJSON_Delete(payload);
        return ESP_FAIL;
    }

    for (size_t i = 0; i < count; ++i) {
        if (addresses[i]) {
            cJSON_AddItemToArray(array, cJSON_CreateString(addresses[i]));
        }
    }

    cJSON_AddItemToObject(payload, "multicast", array);
    return broadcast_message("info", "ipv6.multicast_addresses", payload);
}

esp_err_t broadcast_info_meshcop_service_status_message(bool is_published) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddBoolToObject(payload, "published", is_published);
    return broadcast_message("info", "thread.meshcop_service", payload);
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

esp_err_t broadcast_info_active_dataset_message(
    const uint64_t active_timestamp,
    const char *network_name,
    const uint8_t *extended_pan_id,
    const uint8_t *mesh_local_prefix,
    const uint16_t pan_id,
    const uint16_t channel
) {
    if (!network_name || !extended_pan_id || !mesh_local_prefix) {
        ESP_LOGE(TAG, "Invalid dataset values");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    char extended_pan_id_hex[17] = {};
    char mesh_local_prefix_hex[33] = {};

    binary_to_hex_string(extended_pan_id, 8, extended_pan_id_hex, sizeof(extended_pan_id_hex));
    binary_to_hex_string(mesh_local_prefix, 8, mesh_local_prefix_hex, sizeof(mesh_local_prefix_hex));

    cJSON_AddNumberToObject(payload, "active_timestamp", static_cast<double>(active_timestamp));
    cJSON_AddStringToObject(payload, "network_name", network_name);
    cJSON_AddStringToObject(payload, "extended_pan_id", extended_pan_id_hex);
    cJSON_AddStringToObject(payload, "mesh_local_prefix", mesh_local_prefix_hex);
    cJSON_AddNumberToObject(payload, "pan_id", pan_id);
    cJSON_AddNumberToObject(payload, "channel", channel);

    return broadcast_message("info", "thread.active_dataset", payload);
}

// ---- WI-FI

esp_err_t broadcast_info_wifi_status_message(const char *status) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddStringToObject(payload, "status", status);
    return broadcast_message("info", "wifi.sta_status", payload);
}

// ---- MATTER ----

esp_err_t broadcast_info_matter_commissioning_complete_message(const uint64_t nodeId, const uint8_t fabricIndex) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddNumberToObject(payload, "node_id", nodeId);
    cJSON_AddNumberToObject(payload, "fabric_index", fabricIndex);

    return broadcast_message("info", "matter.commissioning_complete", payload);
}

static esp_err_t broadcast_matter_attribute_report_now(
    const uint64_t nodeId,
    const uint16_t endpointId,
    const uint32_t clusterId,
    const uint32_t attributeId,
    const char *value
) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    char node_id[24];
    snprintf(node_id, sizeof(node_id), "%" PRIu64, nodeId);

    DeviceRecord record = {};
    DeviceCapability capability = {};
    if (device_registry_find_capability_by_path(nodeId, endpointId, clusterId, attributeId,
                                                &record, &capability) == ESP_OK) {
        cJSON_AddStringToObject(payload, "device_id", record.device_id);
        cJSON_AddStringToObject(payload, "capability_id", capability.capability_id);
        cJSON_AddStringToObject(payload, "semantic_type",
                                device_registry_semantic_type_to_string(capability.semantic_type));
        const int raw_value = value ? atoi(value) : 0;
        if (capability.semantic_type == DEVICE_CAPABILITY_TEMPERATURE) {
            cJSON_AddNumberToObject(payload, "temperature_celsius", raw_value / 100.0);
            cJSON_AddNumberToObject(payload, "raw_measured_value", raw_value);
        } else if (capability.semantic_type == DEVICE_CAPABILITY_PRESSURE) {
            cJSON_AddNumberToObject(payload, "pressure_kpa", raw_value / 10.0);
            cJSON_AddNumberToObject(payload, "raw_measured_value", raw_value);
        }
    }

    cJSON_AddStringToObject(payload, "node_id", node_id);
    cJSON_AddNumberToObject(payload, "endpoint_id", endpointId);
    cJSON_AddNumberToObject(payload, "cluster_id", clusterId);
    cJSON_AddNumberToObject(payload, "attribute_id", attributeId);
    cJSON_AddStringToObject(payload, "value", value ? value : "");

    temperature_control_handle_attribute_report(nodeId, endpointId, clusterId, attributeId, value);

    return broadcast_message("event", "matter.attribute_report", payload);
}

static void matter_report_task(void *) {
    while (true) {
        MatterAttributeReportWork work = {};
        if (xQueueReceive(matter_report_queue, &work, portMAX_DELAY) == pdTRUE) {
            broadcast_matter_attribute_report_now(work.node_id, work.endpoint_id, work.cluster_id,
                                                  work.attribute_id, work.value);
        }
    }
}

static esp_err_t ensure_matter_report_worker(void) {
    if (!matter_report_queue) {
        matter_report_queue = xQueueCreate(MATTER_REPORT_QUEUE_LENGTH, sizeof(MatterAttributeReportWork));
        if (!matter_report_queue) return ESP_ERR_NO_MEM;
    }
    if (!matter_report_task_handle) {
        if (xTaskCreate(matter_report_task, "matter_reports", MATTER_REPORT_TASK_STACK_SIZE, nullptr,
                        MATTER_REPORT_TASK_PRIORITY, &matter_report_task_handle) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t enqueue_matter_attribute_report_message(
    const uint64_t nodeId,
    const uint16_t endpointId,
    const uint32_t clusterId,
    const uint32_t attributeId,
    const char *value
) {
    if (!value) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(ensure_matter_report_worker(), TAG, "matter report worker init failed");

    MatterAttributeReportWork work = {};
    work.node_id = nodeId;
    work.endpoint_id = endpointId;
    work.cluster_id = clusterId;
    work.attribute_id = attributeId;
    strncpy(work.value, value, sizeof(work.value) - 1);

    if (xQueueSend(matter_report_queue, &work, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Matter report queue full; dropping attribute report");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t broadcast_info_matter_subscribe_done_message(const uint64_t nodeId, const uint32_t subscription_id) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return ESP_FAIL;

    cJSON_AddNumberToObject(payload, "node_id", nodeId);
    cJSON_AddNumberToObject(payload, "subscription_id", subscription_id);

    return broadcast_message("info", "matter.subscribe_done", payload);
}
