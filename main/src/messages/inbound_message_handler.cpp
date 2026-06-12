#include "messages/inbound_message_handler.h"

#include "commands/matter_commands.h"
#include "commands/thread_cli_commands.h"
#include "commands/thread_commands.h"
#include "commands/wifi_commands.h"
#include "control/temperature_control.h"
#include "matter_interface.h"
#include "matter_controller.h"
#include "matter/matter_discovery.h"
#include "messages/outbound_message_builder.h"
#include "registry/device_registry.h"
#include "sdkconfig.h"
#include "state/orchestrator_state.h"
#include "storage/nvs_diagnostics.h"
#include "thread_util.h"

#include <cJSON.h>
#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <inttypes.h>

static const char *TAG = "JSON_INBOUND_HANDLER";

struct CommandExecutionResult {
    esp_err_t err;
    cJSON *payload;
    bool unknown_action;
    bool deferred;
    char error_code[64];
    char error_message[128];
};

static CommandExecutionResult command_result(esp_err_t err,
                                             cJSON *payload = nullptr,
                                             const char *message = nullptr,
                                             const char *code = nullptr) {
    CommandExecutionResult result = {
        .err = err,
        .payload = payload,
        .unknown_action = false,
        .deferred = false,
        .error_code = {},
        .error_message = {}
    };
    if (code) {
        strncpy(result.error_code, code, sizeof(result.error_code) - 1);
    }
    if (message) {
        strncpy(result.error_message, message, sizeof(result.error_message) - 1);
    }
    return result;
}

static CommandExecutionResult deferred_command_result(void) {
    CommandExecutionResult result = command_result(ESP_OK);
    result.deferred = true;
    return result;
}

static CommandExecutionResult unknown_action_result(const char *action) {
    CommandExecutionResult result = command_result(ESP_ERR_NOT_SUPPORTED);
    result.unknown_action = true;
    snprintf(result.error_message, sizeof(result.error_message), "Unsupported command action: %s", action);
    return result;
}

static bool parse_uint64(const char *s, uint64_t *out) {
    if (!s || !*s) return false;
    if (*s < '0' || *s > '9') return false;
    char *end;
    errno = 0;
    const uint64_t val = strtoull(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE) return false;
    *out = val;
    return true;
}

static bool parse_uint32(const char *s, uint32_t *out) {
    if (!s || !*s) return false;
    if (*s < '0' || *s > '9') return false;
    char *end;
    errno = 0;
    const unsigned long val = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE || val > UINT32_MAX) return false;
    *out = static_cast<uint32_t>(val);
    return true;
}

static bool parse_uint16(const char *s, uint16_t *out) {
    if (!s || !*s) return false;
    if (*s < '0' || *s > '9') return false;
    char *end;
    errno = 0;
    const unsigned long val = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE || val > UINT16_MAX) return false;
    *out = static_cast<uint16_t>(val);
    return true;
}

static const cJSON *required_string_field(const cJSON *payload, const char *name) {
    const cJSON *item = cJSON_GetObjectItem(payload, name);
    if (!cJSON_IsString(item) || item->valuestring == nullptr || item->valuestring[0] == '\0') {
        ESP_LOGW(TAG, "Missing or invalid string field: %s", name);
        return nullptr;
    }
    return item;
}

static const cJSON *optional_string_field(const cJSON *payload, const char *name) {
    const cJSON *item = cJSON_GetObjectItem(payload, name);
    return cJSON_IsString(item) ? item : nullptr;
}

static const cJSON *required_number_field(const cJSON *payload, const char *name) {
    const cJSON *item = cJSON_GetObjectItem(payload, name);
    if (!cJSON_IsNumber(item)) {
        ESP_LOGW(TAG, "Missing or invalid number field: %s", name);
        return nullptr;
    }
    return item;
}

static bool prepare_operational_hint_from_payload(const cJSON *payload,
                                                  uint64_t node_id,
                                                  char *error_message,
                                                  size_t error_message_len) {
    const cJSON *operational_ip = optional_string_field(payload, "operational_ip");
    if (!operational_ip || !operational_ip->valuestring || !operational_ip->valuestring[0]) {
        return true;
    }

    uint16_t operational_port = 5540;
    const cJSON *operational_port_item = cJSON_GetObjectItem(payload, "operational_port");
    if (operational_port_item) {
        if (!cJSON_IsNumber(operational_port_item) ||
            operational_port_item->valueint <= 0 ||
            operational_port_item->valueint > UINT16_MAX) {
            snprintf(error_message, error_message_len, "Invalid Matter operational port");
            return false;
        }
        operational_port = static_cast<uint16_t>(operational_port_item->valueint);
    }

    matter_controller_prepare_operational_address_hint(node_id,
                                                       operational_ip->valuestring,
                                                       operational_port);
    return true;
}

static bool required_uint64_string_field(const cJSON *payload, const char *name, uint64_t *out) {
    const cJSON *item = required_string_field(payload, name);
    if (!item) return false;
    if (!parse_uint64(item->valuestring, out)) {
        ESP_LOGW(TAG, "Invalid uint64 string field: %s", name);
        return false;
    }
    return true;
}

static bool required_uint32_string_field(const cJSON *payload, const char *name, uint32_t *out) {
    const cJSON *item = required_string_field(payload, name);
    if (!item) return false;
    if (!parse_uint32(item->valuestring, out)) {
        ESP_LOGW(TAG, "Invalid uint32 string field: %s", name);
        return false;
    }
    return true;
}

static bool required_uint16_string_field(const cJSON *payload, const char *name, uint16_t *out) {
    const cJSON *item = required_string_field(payload, name);
    if (!item) return false;
    if (!parse_uint16(item->valuestring, out)) {
        ESP_LOGW(TAG, "Invalid uint16 string field: %s", name);
        return false;
    }
    return true;
}

static esp_err_t send_json_to_client(cJSON *root, const int client_fd) {
    char *json = cJSON_PrintUnformatted(root);
    if (!json) return ESP_FAIL;
    esp_err_t err = websocket_send_message_to_client(client_fd, json);
    free(json);
    return err;
}

static esp_err_t send_protocol_error(const int client_fd, const char *code, const char *message) {
    cJSON *root = cJSON_CreateObject();
    cJSON *error = cJSON_CreateObject();
    if (!root || !error) {
        cJSON_Delete(root);
        cJSON_Delete(error);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "error");
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    cJSON_AddItemToObject(root, "error", error);
    esp_err_t err = send_json_to_client(root, client_fd);
    cJSON_Delete(root);
    return err;
}

static const char *error_code_for_result(const CommandExecutionResult &result) {
    if (result.unknown_action) return "UNKNOWN_ACTION";
    if (result.error_code[0]) return result.error_code;
    return esp_err_to_name(result.err);
}

static esp_err_t send_command_result(const int client_fd,
                                     const char *request_id,
                                     const char *action,
                                     CommandExecutionResult result) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(result.payload);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "command_result");
    cJSON_AddStringToObject(root, "request_id", request_id);
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddBoolToObject(root, "ok", result.err == ESP_OK);

    if (result.err == ESP_OK) {
        if (!result.payload) {
            result.payload = cJSON_CreateObject();
            if (!result.payload) {
                cJSON_Delete(root);
                return ESP_ERR_NO_MEM;
            }
        }
        cJSON_AddItemToObject(root, "payload", result.payload);
        result.payload = nullptr;
    } else {
        cJSON_Delete(result.payload);
        cJSON *error = cJSON_CreateObject();
        if (!error) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(error, "code", error_code_for_result(result));
        cJSON_AddStringToObject(error, "message",
                                result.error_message[0] ? result.error_message : esp_err_to_name(result.err));
        cJSON_AddItemToObject(root, "error", error);
    }

    esp_err_t err = send_json_to_client(root, client_fd);
    cJSON_Delete(root);
    return err;
}

static bool add_accepted_payload_field(cJSON *payload, const char *device_id, const char *delivery) {
    return cJSON_AddStringToObject(payload, "device_id", device_id) &&
           cJSON_AddBoolToObject(payload, "accepted", true) &&
           cJSON_AddStringToObject(payload, "result_delivery", delivery);
}

static cJSON *pairing_accepted_payload(uint64_t node_id, const char *transport) {
    cJSON *out = cJSON_CreateObject();
    if (!out) return nullptr;
    char node_id_str[24] = {};
    char device_id[40] = {};
    snprintf(node_id_str, sizeof(node_id_str), "%" PRIu64, node_id);
    snprintf(device_id, sizeof(device_id), "node-%" PRIu64, node_id);
    cJSON_AddStringToObject(out, "node_id", node_id_str);
    cJSON_AddStringToObject(out, "device_id", device_id);
    cJSON_AddStringToObject(out, "transport", transport);
    cJSON_AddBoolToObject(out, "accepted", true);
    cJSON_AddStringToObject(out, "result_delivery", "matter.commissioning_complete");
    return out;
}

static esp_err_t find_capability_by_id(const char *device_id,
                                       const char *capability_id,
                                       DeviceCapabilitySemanticType semantic,
                                       DeviceRecord *record,
                                       DeviceCapability *capability) {
    if (!device_id || !capability_id || !capability) return ESP_ERR_INVALID_ARG;
    DeviceRecord found = {};
    ESP_RETURN_ON_ERROR(device_registry_get_device(device_id, &found), TAG, "device not found");
    for (uint32_t i = 0; i < found.capability_count; ++i) {
        if (found.capabilities[i].semantic_type == semantic &&
            strncmp(found.capabilities[i].capability_id, capability_id, DEVICE_REGISTRY_ID_MAX) == 0) {
            if (record) *record = found;
            *capability = found.capabilities[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t find_single_capability_by_semantic(const char *device_id,
                                                    DeviceCapabilitySemanticType semantic,
                                                    DeviceRecord *record,
                                                    DeviceCapability *capability,
                                                    bool *ambiguous) {
    if (!device_id || !capability) return ESP_ERR_INVALID_ARG;
    if (ambiguous) *ambiguous = false;

    DeviceRecord found = {};
    ESP_RETURN_ON_ERROR(device_registry_get_device(device_id, &found), TAG, "device not found");

    uint32_t match_count = 0;
    DeviceCapability only_match = {};
    for (uint32_t i = 0; i < found.capability_count; ++i) {
        if (found.capabilities[i].semantic_type == semantic) {
            only_match = found.capabilities[i];
            ++match_count;
        }
    }

    if (match_count == 0) return ESP_ERR_NOT_FOUND;
    if (match_count > 1) {
        if (ambiguous) *ambiguous = true;
        return ESP_ERR_INVALID_STATE;
    }

    if (record) *record = found;
    *capability = only_match;
    return ESP_OK;
}

static CommandExecutionResult capability_lookup_result(esp_err_t err, bool ambiguous) {
    if (ambiguous) {
        return command_result(ESP_ERR_INVALID_STATE, nullptr,
                              "Multiple matching capabilities; provide capability_id",
                              "CAPABILITY_AMBIGUOUS");
    }
    if (err == ESP_ERR_NOT_FOUND) {
        return command_result(ESP_ERR_NOT_FOUND, nullptr,
                              "Capability not found",
                              "CAPABILITY_NOT_FOUND");
    }
    return command_result(err, nullptr, "Capability lookup failed");
}

static CommandExecutionResult accepted_device_payload(const char *device_id) {
    cJSON *out = cJSON_CreateObject();
    if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build accepted payload");
    if (!add_accepted_payload_field(out, device_id, "matter.attribute_report")) {
        cJSON_Delete(out);
        return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build accepted payload");
    }
    return command_result(ESP_OK, out);
}

static CommandExecutionResult nvs_stats_command_result(void) {
    cJSON *out = nvs_diagnostics_to_json();
    if (!out) {
        return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build NVS stats payload");
    }
    return command_result(ESP_OK, out);
}

static CommandExecutionResult thread_dataset_init_result(esp_err_t err) {
    switch (err) {
        case ESP_OK:
            return command_result(ESP_OK);
        case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
            return command_result(err, nullptr,
                                  "OpenThread settings storage is full. The active dataset cannot be saved.",
                                  "THREAD_STORAGE_FULL");
        case ESP_ERR_INVALID_ARG:
            return command_result(err, nullptr,
                                  "Thread dataset payload is invalid.",
                                  "INVALID_THREAD_DATASET");
        case ESP_ERR_INVALID_STATE:
            return command_result(err, nullptr,
                                  "OpenThread platform is not initialized.",
                                  "THREAD_PLATFORM_NOT_INITIALIZED");
        case ESP_FAIL:
            return command_result(err, nullptr,
                                  "Thread active dataset could not be initialized.",
                                  "THREAD_DATASET_INIT_FAILED");
        default:
            return command_result(err);
    }
}

static cJSON *capability_to_json(const DeviceCapability &capability) {
    cJSON *item = cJSON_CreateObject();
    if (!item) return nullptr;
    cJSON_AddStringToObject(item, "capability_id", capability.capability_id);
    cJSON_AddStringToObject(item, "semantic_type", device_registry_semantic_type_to_string(capability.semantic_type));
    cJSON_AddNumberToObject(item, "endpoint_id", capability.endpoint_id);
    cJSON_AddNumberToObject(item, "cluster_id", capability.cluster_id);
    if (capability.semantic_type != DEVICE_CAPABILITY_RAW_COMMAND) {
        cJSON_AddNumberToObject(item, "attribute_id", capability.attribute_id);
    }
    if (capability.semantic_type == DEVICE_CAPABILITY_RELAY ||
        capability.semantic_type == DEVICE_CAPABILITY_RAW_COMMAND) {
        cJSON_AddNumberToObject(item, "command_id", capability.command_id);
    }
    cJSON_AddStringToObject(item, "label", capability.label);
    return item;
}

static cJSON *device_to_json(const DeviceRecord &record) {
    cJSON *item = cJSON_CreateObject();
    if (!item) return nullptr;
    char node_id[24];
    snprintf(node_id, sizeof(node_id), "%" PRIu64, record.node_id);
    cJSON_AddStringToObject(item, "device_id", record.device_id);
    cJSON_AddStringToObject(item, "node_id", node_id);
    cJSON_AddStringToObject(item, "label", record.label);
    cJSON_AddBoolToObject(item, "reachable", record.reachable);
    cJSON_AddNumberToObject(item, "vendor_id", record.vendor_id);
    cJSON_AddNumberToObject(item, "product_id", record.product_id);
    cJSON_AddStringToObject(item, "product_name", record.product_name);
    cJSON_AddStringToObject(item, "location", record.location);
    cJSON *capabilities = cJSON_AddArrayToObject(item, "capabilities");
    if (!capabilities) {
        cJSON_Delete(item);
        return nullptr;
    }
    for (uint32_t i = 0; i < record.capability_count; ++i) {
        cJSON *capability = capability_to_json(record.capabilities[i]);
        if (!capability) {
            cJSON_Delete(item);
            return nullptr;
        }
        cJSON_AddItemToArray(capabilities, capability);
    }
    return item;
}

static void copy_field(char *dest, const char *src, size_t len) {
    if (!dest || len == 0) return;
    if (!src) src = "";
    strncpy(dest, src, len - 1);
    dest[len - 1] = '\0';
}

static CommandExecutionResult add_capability_command(const cJSON *payload) {
    const cJSON *device_id = required_string_field(payload, "device_id");
    const cJSON *capability_id = required_string_field(payload, "capability_id");
    const cJSON *semantic_type = required_string_field(payload, "semantic_type");
    const cJSON *endpoint_id = required_number_field(payload, "endpoint_id");
    const cJSON *cluster_id = required_number_field(payload, "cluster_id");
    const cJSON *label = optional_string_field(payload, "label");
    if (!device_id || !capability_id || !semantic_type || !endpoint_id || !cluster_id) {
        return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid capability payload");
    }

    DeviceCapabilitySemanticType semantic;
    if (!device_registry_semantic_type_from_string(semantic_type->valuestring, &semantic)) {
        return command_result(ESP_ERR_INVALID_ARG, nullptr, "Unsupported capability semantic_type");
    }

    DeviceRecord record = {};
    esp_err_t err = device_registry_get_device(device_id->valuestring, &record);
    if (err != ESP_OK) return command_result(err, nullptr, "Device not found");

    DeviceCapability capability = {};
    copy_field(capability.capability_id, capability_id->valuestring, sizeof(capability.capability_id));
    capability.semantic_type = semantic;
    capability.endpoint_id = static_cast<uint16_t>(endpoint_id->valueint);
    capability.cluster_id = static_cast<uint32_t>(cluster_id->valueint);
    capability.attribute_id = UINT32_MAX;
    capability.command_id = UINT32_MAX;

    const cJSON *attribute_id = cJSON_GetObjectItem(payload, "attribute_id");
    if (cJSON_IsNumber(attribute_id)) capability.attribute_id = static_cast<uint32_t>(attribute_id->valueint);
    const cJSON *command_id = cJSON_GetObjectItem(payload, "command_id");
    if (cJSON_IsNumber(command_id)) capability.command_id = static_cast<uint32_t>(command_id->valueint);
    copy_field(capability.label, label ? label->valuestring : semantic_type->valuestring, sizeof(capability.label));

    if ((semantic == DEVICE_CAPABILITY_TEMPERATURE ||
         semantic == DEVICE_CAPABILITY_PRESSURE ||
         semantic == DEVICE_CAPABILITY_RAW_ATTRIBUTE) &&
        capability.attribute_id == UINT32_MAX) {
        return command_result(ESP_ERR_INVALID_ARG, nullptr, "Capability requires attribute_id");
    }
    if (semantic == DEVICE_CAPABILITY_RELAY && capability.command_id == UINT32_MAX) {
        capability.command_id = 0x01;
    }

    for (uint32_t i = 0; i < record.capability_count; ++i) {
        if (strncmp(record.capabilities[i].capability_id, capability.capability_id, DEVICE_REGISTRY_ID_MAX) == 0) {
            record.capabilities[i] = capability;
            err = device_registry_upsert_device(&record);
            if (err != ESP_OK) return command_result(err, nullptr, "Capability upsert failed");
            cJSON *out = cJSON_CreateObject();
            if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build capability payload");
            cJSON_AddStringToObject(out, "device_id", record.device_id);
            cJSON_AddStringToObject(out, "capability_id", capability.capability_id);
            return command_result(ESP_OK, out);
        }
    }

    if (record.capability_count >= DEVICE_REGISTRY_CAPABILITY_MAX) {
        return command_result(ESP_ERR_NO_MEM, nullptr, "Device capability list is full");
    }
    record.capabilities[record.capability_count++] = capability;
    err = device_registry_upsert_device(&record);
    if (err != ESP_OK) return command_result(err, nullptr, "Capability upsert failed");

    cJSON *out = cJSON_CreateObject();
    if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build capability payload");
    cJSON_AddStringToObject(out, "device_id", record.device_id);
    cJSON_AddStringToObject(out, "capability_id", capability.capability_id);
    return command_result(ESP_OK, out);
}

static CommandExecutionResult process_command_message(const char *action,
                                                      const cJSON *payload,
                                                      const char *request_id,
                                                      int client_fd) {
    ESP_LOGI(TAG, "Processing command action: %s", action);

    if (strcmp(action, "system.nvs_stats_get") == 0 ||
        strcmp(action, "thread.storage_status_get") == 0) {
        return nvs_stats_command_result();
    }

#if CONFIG_OPENTHREAD_ENABLED
    if (strcmp(action, "thread.cli.capabilities") == 0) {
        cJSON *out = thread_cli_capabilities_json();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build Thread CLI capabilities");
        return command_result(ESP_OK, out);
    }
    if (strcmp(action, "thread.cli.history") == 0) {
        cJSON *out = thread_cli_history_json();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build Thread CLI history");
        return command_result(ESP_OK, out);
    }
    if (strcmp(action, "thread.cli.cancel") == 0) {
        const cJSON *target = required_string_field(payload, "request_id");
        if (!target) return command_result(ESP_ERR_INVALID_ARG, nullptr, "Missing Thread CLI request_id");
        return command_result(thread_cli_cancel(target->valuestring));
    }
    if (thread_cli_is_deferred_action(action)) {
        char error_code[64] = {};
        char error_message[128] = {};
        esp_err_t err = thread_cli_enqueue_wss_action(client_fd,
                                                      request_id,
                                                      action,
                                                      payload,
                                                      error_code,
                                                      sizeof(error_code),
                                                      error_message,
                                                      sizeof(error_message));
        if (err != ESP_OK) {
            return command_result(err,
                                  nullptr,
                                  error_message[0] ? error_message : "Failed to queue Thread CLI command.",
                                  error_code[0] ? error_code : nullptr);
        }
        return deferred_command_result();
    }

    if (strcmp(action, "thread.enable") == 0) {
        esp_err_t err = execute_thread_enable_command();
        if (err == ESP_ERR_NOT_FOUND) {
            return command_result(err, nullptr,
                                  "Thread cannot be enabled until an active dataset is configured",
                                  "THREAD_DATASET_NOT_CONFIGURED");
        }
        return command_result(err);
    }
    if (strcmp(action, "thread.disable") == 0) return command_result(execute_thread_disable_command());

    if (strcmp(action, "thread.dataset.init") == 0) {
        const cJSON *channel = required_number_field(payload, "channel");
        const cJSON *pan_id = required_number_field(payload, "pan_id");
        const cJSON *network_name = required_string_field(payload, "network_name");
        const cJSON *extended_pan_id = required_string_field(payload, "extended_pan_id");
        const cJSON *mesh_local_prefix = required_string_field(payload, "mesh_local_prefix");
        const cJSON *master_key = required_string_field(payload, "master_key");
        const cJSON *pskc = required_string_field(payload, "pskc");
        if (!channel || !pan_id || !network_name || !extended_pan_id || !mesh_local_prefix || !master_key || !pskc) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid Thread dataset init payload");
        }
        return thread_dataset_init_result(
            execute_thread_dataset_init_command(channel->valueint, pan_id->valueint,
                                                network_name->valuestring,
                                                extended_pan_id->valuestring,
                                                mesh_local_prefix->valuestring,
                                                master_key->valuestring,
                                                pskc->valuestring));
    }

    if (strcmp(action, "thread.status_get") == 0) {
        bool is_running = false;
        esp_err_t err = execute_thread_status_get_command(&is_running);
        if (err != ESP_OK) return command_result(err);
        cJSON *out = cJSON_CreateObject();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build Thread status payload");
        cJSON_AddBoolToObject(out, "running", is_running);
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "thread.attached_get") == 0) {
        bool is_attached = false;
        esp_err_t err = execute_thread_attached_get_command(&is_attached);
        if (err != ESP_OK) return command_result(err);
        cJSON *out = cJSON_CreateObject();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build Thread attached payload");
        cJSON_AddBoolToObject(out, "attached", is_attached);
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "thread.role_get") == 0) {
        const char *role = nullptr;
        esp_err_t err = execute_thread_role_get_command(&role);
        if (err != ESP_OK) return command_result(err);
        cJSON *out = cJSON_CreateObject();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build Thread role payload");
        cJSON_AddStringToObject(out, "role", role ? role : "unknown");
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "thread.active_dataset_get") == 0) {
        otOperationalDataset dataset;
        esp_err_t err = thread_get_active_dataset(&dataset);
        cJSON *out = cJSON_CreateObject();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build Thread dataset payload");
        if (err != ESP_OK) {
            cJSON_AddBoolToObject(out, "present", false);
            return command_result(ESP_OK, out);
        }
        cJSON_AddBoolToObject(out, "present", true);
        cJSON_AddNumberToObject(out, "active_timestamp", static_cast<double>(dataset.mActiveTimestamp.mSeconds));
        cJSON_AddStringToObject(out, "network_name", reinterpret_cast<const char *>(dataset.mNetworkName.m8));
        cJSON_AddNumberToObject(out, "pan_id", dataset.mPanId);
        cJSON_AddNumberToObject(out, "channel", dataset.mChannel);
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "thread.unicast_addresses_get") == 0 ||
        strcmp(action, "thread.multicast_addresses_get") == 0) {
        char *addresses[THREAD_ADDRESS_LIST_MAX] = {nullptr};
        size_t count = 0;
        esp_err_t err = strcmp(action, "thread.unicast_addresses_get") == 0
            ? execute_thread_unicast_addresses_get_command(addresses, THREAD_ADDRESS_LIST_MAX, &count)
            : execute_thread_multicast_addresses_get_command(addresses, THREAD_ADDRESS_LIST_MAX, &count);
        if (err != ESP_OK) return command_result(err);
        cJSON *out = cJSON_CreateObject();
        if (!out) {
            thread_free_address_list(addresses, count);
            return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build address payload");
        }
        cJSON *array = cJSON_AddArrayToObject(out, strcmp(action, "thread.unicast_addresses_get") == 0 ? "unicast" : "multicast");
        if (!array) {
            cJSON_Delete(out);
            thread_free_address_list(addresses, count);
            return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build address payload");
        }
        for (size_t i = 0; i < count; ++i) {
            if (addresses[i]) cJSON_AddItemToArray(array, cJSON_CreateString(addresses[i]));
        }
        thread_free_address_list(addresses, count);
        return command_result(ESP_OK, out);
    }

#if CONFIG_OPENTHREAD_BORDER_ROUTER
    if (strcmp(action, "thread.br_init") == 0) {
        const esp_err_t err = execute_thread_br_init_command();
        cJSON *out = cJSON_CreateObject();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build Border Router payload");
        cJSON_AddBoolToObject(out, "border_router_initialized", err == ESP_OK);
        if (err == ESP_OK) orchestrator_state_broadcast_snapshot();
        return command_result(err, out);
    }
#endif
    if (strcmp(action, "thread.br_deinit") == 0) {
        const esp_err_t err = execute_thread_br_deinit_command();
        cJSON *out = cJSON_CreateObject();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build Border Router payload");
        cJSON_AddBoolToObject(out, "border_router_initialized", false);
        if (err == ESP_OK) orchestrator_state_broadcast_snapshot();
        return command_result(err, out);
    }
#endif

#if CONFIG_ENABLE_WIFI_STATION
    if (strcmp(action, "wifi.sta_connect") == 0) {
        const cJSON *ssid = required_string_field(payload, "ssid");
        const cJSON *password = required_string_field(payload, "password");
        if (!ssid || !password) return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid Wi-Fi payload");
        return command_result(execute_wifi_sta_connect_command(ssid->valuestring, password->valuestring));
    }
#endif

    if (strcmp(action, "chamber.status_get") == 0) {
        cJSON *out = nullptr;
        if (temperature_control_get_chamber(&out) != ESP_OK) {
            return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build chamber payload");
        }
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "chamber.assignment.set") == 0 ||
        strcmp(action, "control.temperature.upsert") == 0) {
        const cJSON *rule_id = optional_string_field(payload, "rule_id");
        const cJSON *chamber_id = optional_string_field(payload, "chamber_id");
        const cJSON *enabled = cJSON_GetObjectItem(payload, "enabled");
        const cJSON *sensor = cJSON_GetObjectItem(payload, "sensor");
        const cJSON *actuator = cJSON_GetObjectItem(payload, "actuator");
        const cJSON *min = required_number_field(payload, "min_celsius");
        const cJSON *max = required_number_field(payload, "max_celsius");
        const cJSON *mode = optional_string_field(payload, "mode");
        if (!cJSON_IsObject(sensor) || !cJSON_IsObject(actuator) || !min || !max ||
            (mode && strcmp(mode->valuestring, "cooling") != 0)) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid temperature control payload");
        }
        const cJSON *sensor_device = required_string_field(sensor, "device_id");
        const cJSON *sensor_cap = required_string_field(sensor, "capability_id");
        const cJSON *actuator_device = required_string_field(actuator, "device_id");
        const cJSON *actuator_cap = required_string_field(actuator, "capability_id");
        if (!sensor_device || !sensor_cap || !actuator_device || !actuator_cap) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid temperature control capability references");
        }
        esp_err_t err = temperature_control_upsert_rule(rule_id ? rule_id->valuestring : "main-air-temperature-fan",
                                                       chamber_id ? chamber_id->valuestring : "main",
                                                       cJSON_IsBool(enabled) ? cJSON_IsTrue(enabled) : true,
                                                       sensor_device->valuestring,
                                                       sensor_cap->valuestring,
                                                       actuator_device->valuestring,
                                                       actuator_cap->valuestring,
                                                       min->valuedouble,
                                                       max->valuedouble);
        if (err != ESP_OK) return command_result(err, nullptr, "Temperature control rule rejected");
        cJSON *out = nullptr;
        if (temperature_control_get_rule(&out) != ESP_OK) return command_result(ESP_ERR_NO_MEM);
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "control.temperature.get") == 0) {
        cJSON *out = nullptr;
        esp_err_t err = temperature_control_get_rule(&out);
        return command_result(err, out);
    }

    if (strcmp(action, "control.temperature.set_enabled") == 0) {
        const cJSON *enabled = cJSON_GetObjectItem(payload, "enabled");
        if (!cJSON_IsBool(enabled)) return command_result(ESP_ERR_INVALID_ARG, nullptr, "Missing enabled boolean");
        return command_result(temperature_control_set_enabled(cJSON_IsTrue(enabled)));
    }

    if (strcmp(action, "control.temperature.delete") == 0 ||
        strcmp(action, "chamber.assignment.clear") == 0) {
        return command_result(temperature_control_delete_rule());
    }

    if (strcmp(action, "device.list") == 0) {
        cJSON *out = device_registry_to_json();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build device registry payload");
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "device.get") == 0) {
        const cJSON *device_id = required_string_field(payload, "device_id");
        if (!device_id) return command_result(ESP_ERR_INVALID_ARG, nullptr, "Missing device_id");
        DeviceRecord record = {};
        esp_err_t err = device_registry_get_device(device_id->valuestring, &record);
        if (err != ESP_OK) return command_result(err, nullptr, "Device not found");
        cJSON *out = device_to_json(record);
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build device payload");
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "device.refresh") == 0) {
        const cJSON *device_id = required_string_field(payload, "device_id");
        if (!device_id) return command_result(ESP_ERR_INVALID_ARG, nullptr, "Missing device_id");
        esp_err_t err = matter_discovery_refresh_device(device_id->valuestring);
        if (err != ESP_OK) return command_result(err, nullptr, "Matter discovery failed");
        cJSON *out = cJSON_CreateObject();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build refresh payload");
        cJSON_AddStringToObject(out, "device_id", device_id->valuestring);
        cJSON_AddBoolToObject(out, "accepted", true);
        cJSON_AddStringToObject(out, "result_delivery", "device.registry_changed");
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "device.remove") == 0) {
        const cJSON *device_id = required_string_field(payload, "device_id");
        if (!device_id) return command_result(ESP_ERR_INVALID_ARG, nullptr, "Missing device_id");
        return command_result(device_registry_remove_device(device_id->valuestring));
    }

    if (strcmp(action, "device.rename") == 0) {
        const cJSON *device_id = required_string_field(payload, "device_id");
        const cJSON *label = required_string_field(payload, "label");
        if (!device_id || !label) return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid rename payload");
        return command_result(device_registry_rename_device(device_id->valuestring, label->valuestring));
    }

    if (strcmp(action, "device.capability.add") == 0) {
        return add_capability_command(payload);
    }

    if (strcmp(action, "device.temperature.read") == 0 ||
        strcmp(action, "device.pressure.read") == 0) {
        const cJSON *device_id = required_string_field(payload, "device_id");
        if (!device_id) return command_result(ESP_ERR_INVALID_ARG, nullptr, "Missing device_id");
        const cJSON *capability_id = optional_string_field(payload, "capability_id");
        const DeviceCapabilitySemanticType semantic = strcmp(action, "device.temperature.read") == 0
            ? DEVICE_CAPABILITY_TEMPERATURE
            : DEVICE_CAPABILITY_PRESSURE;
        DeviceRecord record = {};
        DeviceCapability capability = {};
        bool ambiguous = false;
        esp_err_t err = capability_id
            ? find_capability_by_id(device_id->valuestring, capability_id->valuestring, semantic, &record, &capability)
            : find_single_capability_by_semantic(device_id->valuestring, semantic, &record, &capability, &ambiguous);
        if (err != ESP_OK) return capability_lookup_result(err, ambiguous);
        char hint_error[96] = {};
        if (!prepare_operational_hint_from_payload(payload, record.node_id, hint_error, sizeof(hint_error))) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, hint_error);
        }
        err = execute_attr_read_command(record.node_id, capability.endpoint_id, capability.cluster_id, capability.attribute_id);
        if (err != ESP_OK) return command_result(err);
        return accepted_device_payload(device_id->valuestring);
    }

    if (strcmp(action, "device.attribute.read") == 0 ||
        strcmp(action, "device.attribute.subscribe") == 0) {
        const cJSON *device_id = required_string_field(payload, "device_id");
        const cJSON *endpoint = required_number_field(payload, "endpoint_id");
        const cJSON *cluster = required_number_field(payload, "cluster_id");
        const cJSON *attr = required_number_field(payload, "attribute_id");
        if (!device_id || !endpoint || !cluster || !attr) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid attribute payload");
        }
        DeviceRecord record = {};
        esp_err_t err = device_registry_get_device(device_id->valuestring, &record);
        if (err != ESP_OK) return command_result(err, nullptr, "Device not found");
        char hint_error[96] = {};
        if (!prepare_operational_hint_from_payload(payload, record.node_id, hint_error, sizeof(hint_error))) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, hint_error);
        }
        if (strcmp(action, "device.attribute.subscribe") == 0) {
            const cJSON *min = required_number_field(payload, "min_interval");
            const cJSON *max = required_number_field(payload, "max_interval");
            if (!min || !max) return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid subscribe interval payload");
            err = execute_attr_subscribe_command(record.node_id,
                                                 static_cast<uint16_t>(endpoint->valueint),
                                                 static_cast<uint32_t>(cluster->valueint),
                                                 static_cast<uint32_t>(attr->valueint),
                                                 static_cast<uint16_t>(min->valueint),
                                                 static_cast<uint16_t>(max->valueint));
        } else {
            err = execute_attr_read_command(record.node_id,
                                            static_cast<uint16_t>(endpoint->valueint),
                                            static_cast<uint32_t>(cluster->valueint),
                                            static_cast<uint32_t>(attr->valueint));
        }
        if (err != ESP_OK) return command_result(err);
        return accepted_device_payload(device_id->valuestring);
    }

    if (strcmp(action, "device.relay.set") == 0) {
        const cJSON *device_id = required_string_field(payload, "device_id");
        const cJSON *capability_id = optional_string_field(payload, "capability_id");
        const cJSON *on = cJSON_GetObjectItem(payload, "on");
        if (!device_id || !cJSON_IsBool(on)) return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid relay payload");
        DeviceRecord record = {};
        DeviceCapability capability = {};
        bool ambiguous = false;
        esp_err_t err = capability_id
            ? find_capability_by_id(device_id->valuestring, capability_id->valuestring, DEVICE_CAPABILITY_RELAY, &record, &capability)
            : find_single_capability_by_semantic(device_id->valuestring, DEVICE_CAPABILITY_RELAY, &record, &capability, &ambiguous);
        if (err != ESP_OK) return capability_lookup_result(err, ambiguous);
        char hint_error[96] = {};
        if (!prepare_operational_hint_from_payload(payload, record.node_id, hint_error, sizeof(hint_error))) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, hint_error);
        }
        err = execute_cmd_invoke_command(record.node_id, capability.endpoint_id, capability.cluster_id,
                                         cJSON_IsTrue(on) ? 0x01 : 0x00, "{}");
        if (err != ESP_OK) return command_result(err);
        temperature_control_note_manual_relay_command(device_id->valuestring, capability.capability_id, cJSON_IsTrue(on));
        cJSON *out = cJSON_CreateObject();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build relay payload");
        cJSON_AddStringToObject(out, "device_id", device_id->valuestring);
        cJSON_AddStringToObject(out, "capability_id", capability.capability_id);
        cJSON_AddBoolToObject(out, "accepted", true);
        cJSON_AddBoolToObject(out, "on", cJSON_IsTrue(on));
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "matter.platform_reset") == 0) {
        esp_err_t err = matter_interface_platform_reset();
        if (err != ESP_OK) {
            return command_result(err, nullptr, "Failed to reset Matter platform namespaces");
        }
        orchestrator_state_set_matter_platform_initialized(false);
        orchestrator_state_set_matter_platform_error(matter_interface_get_last_error());
        cJSON *out = cJSON_CreateObject();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build Matter platform reset payload");
        cJSON_AddBoolToObject(out, "accepted", true);
        cJSON_AddBoolToObject(out, "reboot_required", true);
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "matter.controller_init") == 0) {
        bool platform_initialized = false;
        char platform_error[192] = {};
        if (orchestrator_state_get_matter_platform_status(&platform_initialized,
                                                          platform_error,
                                                          sizeof(platform_error)) != ESP_OK ||
            !platform_initialized) {
            char message[160] = {};
            snprintf(message, sizeof(message), "Matter platform is not initialized: %s",
                     platform_error[0] ? platform_error : "unknown platform error");
            return command_result(ESP_ERR_INVALID_STATE, nullptr, message, "MATTER_PLATFORM_NOT_INITIALIZED");
        }

        uint64_t node_id_val;
        const cJSON *fabric_id = required_number_field(payload, "fabric_id");
        const cJSON *listen_port = required_number_field(payload, "listen_port");
        if (!required_uint64_string_field(payload, "node_id", &node_id_val) || !fabric_id || !listen_port) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid Matter init payload");
        }
        char error_code[64] = {};
        char error_message[128] = {};
        esp_err_t err = matter_command_enqueue_controller_init(client_fd,
                                                               request_id,
                                                               node_id_val,
                                                               static_cast<uint64_t>(fabric_id->valuedouble),
                                                               static_cast<uint16_t>(listen_port->valueint),
                                                               error_code,
                                                               sizeof(error_code),
                                                               error_message,
                                                               sizeof(error_message));
        if (err != ESP_OK) {
            return command_result(err,
                                  nullptr,
                                  error_message[0] ? error_message : "Failed to queue Matter controller init.",
                                  error_code[0] ? error_code : nullptr);
        }
        return deferred_command_result();
    }

    if (strcmp(action, "matter.pair_ble_thread") == 0) {
        uint64_t node_id_val;
        uint32_t pin;
        uint16_t disc;
        if (!required_uint64_string_field(payload, "node_id", &node_id_val) ||
            !required_uint32_string_field(payload, "setup_code", &pin) ||
            !required_uint16_string_field(payload, "discriminator", &disc)) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid BLE pairing payload");
        }
        const cJSON *operational_ip = optional_string_field(payload, "operational_ip");
        uint16_t operational_port = 5540;
        const cJSON *operational_port_item = cJSON_GetObjectItem(payload, "operational_port");
        if (operational_port_item) {
            if (!cJSON_IsNumber(operational_port_item) ||
                operational_port_item->valueint <= 0 ||
                operational_port_item->valueint > UINT16_MAX) {
                return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid Matter operational port");
            }
            operational_port = static_cast<uint16_t>(operational_port_item->valueint);
        }
        esp_err_t err = execute_matter_pair_ble_thread_command(node_id_val,
                                                               pin,
                                                               disc,
                                                               operational_ip ? operational_ip->valuestring : nullptr,
                                                               operational_port);
        if (err != ESP_OK) return command_result(err);
        cJSON *out = pairing_accepted_payload(node_id_val, "ble_thread");
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build pairing payload");
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "matter.pair_ble_wifi") == 0) {
        uint64_t node_id_val;
        uint32_t pin;
        uint16_t disc;
        const cJSON *ssid = required_string_field(payload, "ssid");
        const cJSON *password = required_string_field(payload, "password");
        if (!required_uint64_string_field(payload, "node_id", &node_id_val) ||
            !required_uint32_string_field(payload, "setup_code", &pin) ||
            !required_uint16_string_field(payload, "discriminator", &disc) ||
            !ssid || !password) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid BLE Wi-Fi pairing payload");
        }
        esp_err_t err = execute_matter_pair_ble_wifi_command(node_id_val,
                                                             pin,
                                                             disc,
                                                             ssid->valuestring,
                                                             password->valuestring);
        if (err != ESP_OK) return command_result(err);
        cJSON *out = pairing_accepted_payload(node_id_val, "ble_wifi");
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build pairing payload");
        return command_result(ESP_OK, out);
    }

    if (strcmp(action, "matter.cluster_command_invoke") == 0) {
        uint64_t destination_id_val;
        const cJSON *ep = required_number_field(payload, "endpoint_id");
        const cJSON *cluster = required_number_field(payload, "cluster_id");
        const cJSON *cmd = required_number_field(payload, "command_id");
        const cJSON *data = required_string_field(payload, "command_data");
        if (!required_uint64_string_field(payload, "destination_id", &destination_id_val) || !ep || !cluster || !cmd || !data) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid invoke payload");
        }
        char hint_error[96] = {};
        if (!prepare_operational_hint_from_payload(payload, destination_id_val, hint_error, sizeof(hint_error))) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, hint_error);
        }
        return command_result(execute_cmd_invoke_command(destination_id_val,
                                                         static_cast<uint16_t>(ep->valueint),
                                                         static_cast<uint32_t>(cluster->valueint),
                                                         static_cast<uint32_t>(cmd->valueint),
                                                         data->valuestring));
    }

    if (strcmp(action, "matter.attribute_read") == 0 ||
        strcmp(action, "matter.attribute_subscribe") == 0) {
        uint64_t node_id_val;
        const cJSON *ep = required_number_field(payload, "endpoint_id");
        const cJSON *cluster = required_number_field(payload, "cluster_id");
        const cJSON *attr = required_number_field(payload, "attribute_id");
        if (!required_uint64_string_field(payload, "node_id", &node_id_val) || !ep || !cluster || !attr) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid attribute payload");
        }
        char hint_error[96] = {};
        if (!prepare_operational_hint_from_payload(payload, node_id_val, hint_error, sizeof(hint_error))) {
            return command_result(ESP_ERR_INVALID_ARG, nullptr, hint_error);
        }
        esp_err_t err;
        if (strcmp(action, "matter.attribute_subscribe") == 0) {
            const cJSON *min = required_number_field(payload, "min_interval");
            const cJSON *max = required_number_field(payload, "max_interval");
            if (!min || !max) return command_result(ESP_ERR_INVALID_ARG, nullptr, "Invalid subscribe payload");
            err = execute_attr_subscribe_command(node_id_val,
                                                 static_cast<uint16_t>(ep->valueint),
                                                 static_cast<uint32_t>(cluster->valueint),
                                                 static_cast<uint32_t>(attr->valueint),
                                                 static_cast<uint16_t>(min->valueint),
                                                 static_cast<uint16_t>(max->valueint));
        } else {
            err = execute_attr_read_command(node_id_val,
                                            static_cast<uint16_t>(ep->valueint),
                                            static_cast<uint32_t>(cluster->valueint),
                                            static_cast<uint32_t>(attr->valueint));
        }
        if (err != ESP_OK) return command_result(err);
        cJSON *out = cJSON_CreateObject();
        if (!out) return command_result(ESP_ERR_NO_MEM, nullptr, "Failed to build accepted payload");
        cJSON_AddBoolToObject(out, "accepted", true);
        cJSON_AddStringToObject(out, "result_delivery", "matter.attribute_report");
        return command_result(ESP_OK, out);
    }

    return unknown_action_result(action);
}

esp_err_t handle_json_inbound_message(const char *inbound_message, const int client_fd) {
    if (!inbound_message) {
        ESP_LOGE(TAG, "Null inbound message");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(inbound_message);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error");
        return send_protocol_error(client_fd, "INVALID_JSON", "Message is not valid JSON");
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");

    esp_err_t ret = ESP_OK;
    if (!cJSON_IsString(type)) {
        ret = send_protocol_error(client_fd, "MISSING_TYPE", "Missing required field: type");
    } else if (strcmp(type->valuestring, "command") != 0) {
        ret = send_protocol_error(client_fd, "UNSUPPORTED_TYPE", "Unsupported message type");
    } else if (!cJSON_IsString(request_id) || !request_id->valuestring[0]) {
        ret = send_protocol_error(client_fd, "MISSING_REQUEST_ID", "Command messages require request_id");
    } else if (!cJSON_IsString(action) || !action->valuestring[0]) {
        ret = send_protocol_error(client_fd, "MISSING_ACTION", "Command messages require action");
    } else if (!cJSON_IsObject(payload)) {
        ret = send_protocol_error(client_fd, "INVALID_PAYLOAD", "Command payload must be an object");
    } else {
        CommandExecutionResult result = process_command_message(action->valuestring,
                                                               payload,
                                                               request_id->valuestring,
                                                               client_fd);
        ret = result.deferred ? ESP_OK
                              : send_command_result(client_fd, request_id->valuestring, action->valuestring, result);
    }

    cJSON_Delete(root);
    return ret;
}

void handle_websocket_client_event(ws_client_event_t event, int client_fd) {
    orchestrator_state_set_websocket_client_count(websocket_server_client_count());
    if (event == WS_CLIENT_CONNECTED) {
        orchestrator_state_send_snapshot_to_client(client_fd);
        cJSON *payload = cJSON_CreateObject();
        if (payload) cJSON_AddNumberToObject(payload, "clients", websocket_server_client_count());
        orchestrator_state_broadcast_event("websocket.client_connected", payload);
    } else if (event == WS_CLIENT_DISCONNECTED) {
        cJSON *payload = cJSON_CreateObject();
        if (payload) cJSON_AddNumberToObject(payload, "clients", websocket_server_client_count());
        orchestrator_state_broadcast_event("websocket.client_disconnected", payload);
        orchestrator_state_broadcast_snapshot();
    }
}
