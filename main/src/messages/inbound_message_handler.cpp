#include "messages/inbound_message_handler.h"
#include "commands/matter_commands.h"
#include "commands/wifi_commands.h"
#include "commands/thread_commands.h"
#include "messages/outbound_message_builder.h"
#include "sdkconfig.h"
#include "thread_util.h"

#include <cJSON.h>
#include <esp_event.h>
#include <esp_log.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>

static const char *TAG = "JSON_INBOUND_HANDLER";

/**
 * Parses a string representing an unsigned 64-bit integer and stores the result.
 *
 * @param s The input string to parse. Must represent a valid unsigned 64-bit integer.
 * @param out A pointer to a variable where the parsed value will be stored upon success.
 *
 * @return true if the string was successfully parsed as an unsigned 64-bit integer, false otherwise.
 *         Failure scenarios include null or empty input string, invalid characters in the input, or
 *         parsing errors.
 */
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

/**
 * Parses a null-terminated string into an unsigned 32-bit integer.
 *
 * Converts the input string `s` into a `uint32_t` value if it represents a valid
 * non-negative integer within the range [0, UINT32_MAX]. The result is stored in
 * the location pointed to by `out` if parsing succeeds.
 *
 * @param[in] s A pointer to a null-terminated string containing the input to parse.
 *              Must not be null, and must contain at least one character.
 * @param[out] out A pointer to a `uint32_t` variable where the parsed value will be stored
 *                 if the function succeeds. Must not be null.
 *
 * @retval true If the parsing is successful, and `*out` contains the parsed value.
 * @retval false If parsing fails due to an invalid input, overflow, or other errors.
 */
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

/**
 * Parses a string into an unsigned 16-bit integer.
 *
 * This function attempts to convert the input string to an unsigned 16-bit
 * integer. The conversion succeeds only if the entire string represents a
 * valid number within the range of a uint16_t.
 *
 * @param s Pointer to a null-terminated string containing the input number.
 *          Should not be null, and must represent a valid numeric value.
 * @param out Pointer to a uint16_t variable where the parsed result is stored
 *            upon successful conversion. Must not be null.
 *
 * @return True if the string is successfully converted to a uint16_t and false
 *         otherwise. Returns false if the input string is null, empty, contains
 *         non-numeric characters, or represents a number outside the uint16_t range.
 */
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
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        ESP_LOGW(TAG, "Missing or invalid string field: %s", name);
        return nullptr;
    }
    return item;
}

static const cJSON *required_number_field(const cJSON *payload, const char *name) {
    const cJSON *item = cJSON_GetObjectItem(payload, name);
    if (!cJSON_IsNumber(item)) {
        ESP_LOGW(TAG, "Missing or invalid number field: %s", name);
        return nullptr;
    }
    return item;
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

/**
 * Processes a command message by executing the appropriate action based on the specified command and payload.
 * This function handles various commands related to Thread, Wi-Fi, and Matter functionalities.
 *
 * @param action The command action to be processed. The action specifies the type of operation to execute.
 *               Supported actions are specific to Thread, Wi-Fi, and Matter components (e.g., "thread.enable",
 *               "wifi.sta_connect", "matter.controller_init").
 * @param payload The payload containing the parameters required to execute the specified action. This is expected
 *                to be a cJSON object with key-value pairs relevant to the command action.
 *
 * @return `ESP_OK` if the command was successfully processed, and the corresponding operation was executed,
 *         or an error code defining the failure reason. Potential error cases include invalid arguments,
 *         unsupported actions, or internal execution failures.
 */
static esp_err_t process_command_message(const char *action, const cJSON *payload) {
    ESP_LOGI(TAG, "Processing command action: %s", action);

    // Thread commands defined in thread_command.h
#if CONFIG_OPENTHREAD_ENABLED
    // thread.enable
    if (strcmp(action, "thread.enable") == 0) {
        return execute_thread_enable_command();
    }
    // thread.disable
    if (strcmp(action, "thread.disable") == 0) {
        return execute_thread_disable_command();
    }
    // thread.dataset_init
    if (strcmp(action, "thread.dataset.init") == 0) {
        const cJSON *channel = required_number_field(payload, "channel");
        const cJSON *pan_id = required_number_field(payload, "pan_id");
        const cJSON *network_name = required_string_field(payload, "network_name");
        const cJSON *extended_pan_id = required_string_field(payload, "extended_pan_id");
        const cJSON *mesh_local_prefix = required_string_field(payload, "mesh_local_prefix");
        const cJSON *master_key = required_string_field(payload, "master_key");
        const cJSON *pskc = required_string_field(payload, "pskc");

        if (!channel || !pan_id || !network_name || !extended_pan_id ||
            !mesh_local_prefix || !master_key || !pskc) {
            ESP_LOGW(TAG, "Invalid Thread dataset init payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_thread_dataset_init_command(
            channel->valueint,
            pan_id->valueint,
            network_name->valuestring,
            extended_pan_id->valuestring,
            mesh_local_prefix->valuestring,
            master_key->valuestring,
            pskc->valuestring
        );
    }
    // thread.status_get
    if (strcmp(action, "thread.status_get") == 0) {
        bool is_running;
        esp_err_t ret = execute_thread_status_get_command(&is_running);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Thread status - Running: %s", is_running ? "true" : "false");
            ret = broadcast_info_thread_stack_status_message(is_running);
        }
        return ret;
    }
    // thread.attached_get
    if (strcmp(action, "thread.attached_get") == 0) {
        bool is_attached;
        esp_err_t ret = execute_thread_attached_get_command(&is_attached);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Thread attached state: %s", is_attached ? "attached" : "not attached");
            ret = broadcast_info_thread_attachment_status_message(is_attached);
        }
        return ret;
    }
    // thread.role_get
    if (strcmp(action, "thread.role_get") == 0) {
        const char *role_str;
        esp_err_t ret = execute_thread_role_get_command(&role_str);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Thread role: %s", role_str);
            ret = broadcast_info_thread_role_message(role_str);
        }
        return ret;
    }
    // thread.active_dataset_get
    if (strcmp(action, "thread.active_dataset_get") == 0) {
        otOperationalDataset dataset;
        esp_err_t ret = thread_get_active_dataset(&dataset);
        if (ret == ESP_OK) {
            ret = broadcast_info_active_dataset_message(
                dataset.mActiveTimestamp.mSeconds,
                (const char *)dataset.mNetworkName.m8,
                dataset.mExtendedPanId.m8,
                dataset.mMeshLocalPrefix.m8,
                dataset.mPanId,
                dataset.mChannel
            );
        } else {
            ESP_LOGW(TAG, "Failed to get active dataset");
        }
        return ret;
    }
    // thread.unicast_addresses_get
    if (strcmp(action, "thread.unicast_addresses_get") == 0) {
        char *addresses[THREAD_ADDRESS_LIST_MAX] = {nullptr};
        size_t count = 0;
        esp_err_t ret = execute_thread_unicast_addresses_get_command(addresses, THREAD_ADDRESS_LIST_MAX, &count);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Unicast addresses count: %zu", count);
            ret = broadcast_info_unicast_addresses_message(const_cast<const char **>(addresses), count);
            thread_free_address_list(addresses, count);
        }
        return ret;
    }
    // thread.multicast_addresses_get
    if (strcmp(action, "thread.multicast_addresses_get") == 0) {
        char *addresses[THREAD_ADDRESS_LIST_MAX] = {nullptr};
        size_t count = 0;
        esp_err_t ret = execute_thread_multicast_addresses_get_command(addresses, THREAD_ADDRESS_LIST_MAX, &count);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Multicast addresses count: %zu", count);
            ret = broadcast_info_multicast_addresses_message(const_cast<const char **>(addresses), count);
            thread_free_address_list(addresses, count);
        }
        return ret;
    }
    // thread.br_init
#if CONFIG_OPENTHREAD_BORDER_ROUTER
    if (strcmp(action, "thread.br_init") == 0) {
        return execute_thread_br_init_command();
    }
#endif
    // thread.br_deinit
    if (strcmp(action, "thread.br_deinit") == 0) {
        return execute_thread_br_deinit_command();
    }
#endif

    // Wi-Fi commands defined in wifi_command.h
#if CONFIG_ENABLE_WIFI_STATION
    // wifi.sta_connect
    if (strcmp(action, "wifi.sta_connect") == 0) {
        const cJSON *ssid = required_string_field(payload, "ssid");
        const cJSON *password = required_string_field(payload, "password");
        if (!ssid || !password) {
            ESP_LOGW(TAG, "Invalid Wi-Fi payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_wifi_sta_connect_command(ssid->valuestring, password->valuestring);
    }
#endif

    // Matter commands defined in matter_command.h
    // matter.controller_init
    if (strcmp(action, "matter.controller_init") == 0) {
        uint64_t node_id_val;
        const cJSON *fabric_id = required_number_field(payload, "fabric_id");
        const cJSON *listen_port = required_number_field(payload, "listen_port");
        if (!required_uint64_string_field(payload, "node_id", &node_id_val) ||
            !fabric_id ||
            !listen_port) {
            ESP_LOGW(TAG, "Invalid Matter init payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_matter_controller_init_command(
            node_id_val,
            static_cast<uint64_t>(fabric_id->valuedouble),
            static_cast<uint16_t>(listen_port->valueint)
        );
    }
    // matter.pair_ble_thread
    if (strcmp(action, "matter.pair_ble_thread") == 0) {
        uint64_t node_id_val;
        uint32_t pin;
        uint16_t disc;

        if (!required_uint64_string_field(payload, "node_id", &node_id_val) ||
            !required_uint32_string_field(payload, "setup_code", &pin) ||
            !required_uint16_string_field(payload, "discriminator", &disc)) {
            ESP_LOGW(TAG, "Invalid BLE pairing payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_matter_pair_ble_thread_command(node_id_val, pin, disc);
    }

    // matter.cluster_command_invoke
    if (strcmp(action, "matter.cluster_command_invoke") == 0) {
        uint64_t destination_id_val;
        const cJSON *ep = required_number_field(payload, "endpoint_id");
        const cJSON *cluster = required_number_field(payload, "cluster_id");
        const cJSON *cmd = required_number_field(payload, "command_id");
        const cJSON *data = required_string_field(payload, "command_data");
        if (!required_uint64_string_field(payload, "destination_id", &destination_id_val) ||
            !ep ||
            !cluster ||
            !cmd ||
            !data) {
            ESP_LOGW(TAG, "Invalid invoke payload");
            return ESP_ERR_INVALID_ARG;
        }


        return execute_cmd_invoke_command(destination_id_val,
                                          static_cast<uint16_t>(ep->valueint),
                                          static_cast<uint32_t>(cluster->valueint),
                                          static_cast<uint32_t>(cmd->valueint),
                                          data->valuestring);
    }

    // matter.attribute_read
    if (strcmp(action, "matter.attribute_read") == 0) {
        uint64_t node_id_val;
        const cJSON *ep = required_number_field(payload, "endpoint_id");
        const cJSON *cluster = required_number_field(payload, "cluster_id");
        const cJSON *attr = required_number_field(payload, "attribute_id");
        if (!required_uint64_string_field(payload, "node_id", &node_id_val) ||
            !ep ||
            !cluster ||
            !attr) {
            ESP_LOGW(TAG, "Invalid read-attr payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_attr_read_command(
            node_id_val,
            static_cast<uint16_t>(ep->valueint),
            static_cast<uint32_t>(cluster->valueint),
            static_cast<uint32_t>(attr->valueint));
    }

    // matter.attribute_subscribe
    if (strcmp(action, "matter.attribute_subscribe") == 0) {
        uint64_t node_id_val;
        const cJSON *ep = required_number_field(payload, "endpoint_id");
        const cJSON *cluster = required_number_field(payload, "cluster_id");
        const cJSON *attr = required_number_field(payload, "attribute_id");
        const cJSON *min = required_number_field(payload, "min_interval");
        const cJSON *max = required_number_field(payload, "max_interval");
        if (!required_uint64_string_field(payload, "node_id", &node_id_val) ||
            !ep ||
            !cluster ||
            !attr ||
            !min ||
            !max) {
            ESP_LOGW(TAG, "Invalid subscribe-attr payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_attr_subscribe_command(
            node_id_val,
            static_cast<uint16_t>(ep->valueint),
            static_cast<uint32_t>(cluster->valueint),
            static_cast<uint32_t>(attr->valueint),
            static_cast<uint16_t>(min->valueint),
            static_cast<uint16_t>(max->valueint)
        );
    }

    ESP_LOGW(TAG, "Unknown action");
    return ESP_ERR_INVALID_ARG;
}

esp_err_t handle_json_inbound_message(const char *inbound_message) {
    if (!inbound_message) {
        ESP_LOGE(TAG, "Null inbound message");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(inbound_message);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");

    esp_err_t ret = ESP_OK;

    // Validate the message structure: type, action, payload
    if (!cJSON_IsString(type)) {
        ESP_LOGW(TAG, "Invalid or missing 'type' (expected: 'command')");
        ret = ESP_ERR_INVALID_ARG;
    } else if (!cJSON_IsString(action)) {
        ESP_LOGW(TAG, "Missing or invalid 'action' field");
        ret = ESP_ERR_INVALID_ARG;
    } else if (!cJSON_IsObject(payload)) {
        ESP_LOGW(TAG, "Missing or invalid 'payload' field");
        ret = ESP_ERR_INVALID_ARG;
    }

    // If the message is valid, process it
    if (ret == ESP_OK && strcmp(type->valuestring, "command") == 0) {
        ret = process_command_message(action->valuestring, payload);
    }

    cJSON_Delete(root);
    return ret;
}
