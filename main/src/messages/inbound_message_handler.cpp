#include "messages/inbound_message_handler.h"
#include "commands/matter_commands.h"
#include "commands/wifi_commands.h"
#include "commands/thread_commands.h"
#include "sdkconfig.h"

#include <cJSON.h>
#include <esp_event.h>
#include <esp_log.h>
#include <cstring>

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
    char *end;
    const uint64_t val = strtoull(s, &end, 10);
    if (end == s || *end != '\0') return false;
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
    char *end;
    const unsigned long val = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || val > UINT32_MAX) return false;
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
    char *end;
    const unsigned long val = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || val > UINT16_MAX) return false;
    *out = static_cast<uint16_t>(val);
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
 * @return `ESP_OK` if the command was successfully processed and the corresponding operation was executed,
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
        return execute_thread_dataset_init_command(
            cJSON_GetObjectItem(payload, "channel")->valueint,
            cJSON_GetObjectItem(payload, "pan_id")->valueint,
            cJSON_GetObjectItem(payload, "network_name")->valuestring,
            cJSON_GetObjectItem(payload, "extended_pan_id")->valuestring,
            cJSON_GetObjectItem(payload, "mesh_local_prefix")->valuestring,
            cJSON_GetObjectItem(payload, "master_key")->valuestring,
            cJSON_GetObjectItem(payload, "pskc")->valuestring
        );
    }
    // thread.status_get
    if (strcmp(action, "thread.status_get") == 0) {
        bool is_running;
        esp_err_t ret = execute_thread_status_get_command(&is_running);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Thread status - Running: %s", is_running ? "true" : "false");
        }
        return ret;
    }
    // thread.attached_get
    if (strcmp(action, "thread.attached_get") == 0) {
        bool is_attached;
        esp_err_t ret = execute_thread_attached_get_command(&is_attached);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Thread attached state: %s", is_attached ? "attached" : "not attached");
        }
        return ret;
    }
    // thread.role_get
    if (strcmp(action, "thread.role_get") == 0) {
        const char *role_str;
        esp_err_t ret = execute_thread_role_get_command(&role_str);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Thread role: %s", role_str);
        }
        return ret;
    }
    // thread.active_dataset_get
    if (strcmp(action, "thread.active_dataset_get") == 0) {
        char json_buf[512]; // Example buffer size
        esp_err_t ret = execute_thread_active_dataset_get_command(json_buf, sizeof(json_buf));
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Active Dataset: %s", json_buf);
        }
        return ret;
    }
    // thread.unicast_addresses_get
    if (strcmp(action, "thread.unicast_addresses_get") == 0) {
        char *addresses[10];
        size_t count;
        esp_err_t ret = execute_thread_unicast_addresses_get_command(addresses, 10, &count);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Unicast addresses count: %zu", count);
        }
        return ret;
    }
    // thread.multicast_addresses_get
    if (strcmp(action, "thread.multicast_addresses_get") == 0) {
        char *addresses[10];
        size_t count;
        esp_err_t ret = execute_thread_multicast_addresses_get_command(addresses, 10, &count);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Multicast addresses count: %zu", count);
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
        const cJSON *ssid = cJSON_GetObjectItem(payload, "ssid");
        const cJSON *password = cJSON_GetObjectItem(payload, "password");
        if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) {
            ESP_LOGW(TAG, "Invalid Wi-Fi payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_wifi_sta_connect_command(ssid->valuestring, password->valuestring);
    }
#endif

    // Matter commands defined in matter_command.h
    // matter.controller_init
    if (strcmp(action, "matter.controller_init") == 0) {
        const cJSON *node_id = cJSON_GetObjectItem(payload, "node_id");
        const cJSON *fabric_id = cJSON_GetObjectItem(payload, "fabric_id");
        const cJSON *listen_port = cJSON_GetObjectItem(payload, "listen_port");
        if (!cJSON_IsString(node_id) || !cJSON_IsNumber(fabric_id) || !cJSON_IsNumber(listen_port)) {
            ESP_LOGW(TAG, "Invalid Matter init payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_matter_controller_init_command(
            static_cast<uint64_t>(node_id->valueint),
            static_cast<uint64_t>(fabric_id->valuedouble),
            static_cast<uint16_t>(listen_port->valueint)
        );
    }
    // matter.pair_ble_thread
    if (strcmp(action, "matter.pair_ble_thread") == 0) {
        const cJSON *node_id = cJSON_GetObjectItem(payload, "node_id");
        const cJSON *setup_code = cJSON_GetObjectItem(payload, "setup_code");
        const cJSON *discriminator = cJSON_GetObjectItem(payload, "discriminator");

        uint64_t node_id_val;
        uint32_t pin;
        uint16_t disc;

        if (!parse_uint64(node_id->valuestring, &node_id_val) ||
            !parse_uint32(setup_code->valuestring, &pin) ||
            !parse_uint16(discriminator->valuestring, &disc)) {
            ESP_LOGW(TAG, "BLE pairing values invalid");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_matter_pair_ble_thread_command(node_id_val, pin, disc);
    }

    // matter.cluster_command_invoke
    if (strcmp(action, "matter.cluster_command_invoke") == 0) {
        const cJSON *dest = cJSON_GetObjectItem(payload, "destination_id");
        const cJSON *ep = cJSON_GetObjectItem(payload, "endpoint_id");
        const cJSON *cluster = cJSON_GetObjectItem(payload, "cluster_id");
        const cJSON *cmd = cJSON_GetObjectItem(payload, "command_id");
        const cJSON *data = cJSON_GetObjectItem(payload, "command_data");
        if (!cJSON_IsString(dest) || !cJSON_IsNumber(ep) || !cJSON_IsNumber(cluster) || !cJSON_IsNumber(cmd) || !
            cJSON_IsString(data)) {
            ESP_LOGW(TAG, "Invalid invoke payload");
            return ESP_ERR_INVALID_ARG;
        }


        return execute_cmd_invoke_command(static_cast<uint64_t>(dest->valueint),
                                          static_cast<uint16_t>(ep->valueint),
                                          static_cast<uint32_t>(cluster->valueint),
                                          static_cast<uint32_t>(cmd->valueint),
                                          data->valuestring);
    }

    // matter.attribute_read
    if (strcmp(action, "matter.attribute_read") == 0) {
        const cJSON *node = cJSON_GetObjectItem(payload, "node_id");
        const cJSON *ep = cJSON_GetObjectItem(payload, "endpoint_id");
        const cJSON *cluster = cJSON_GetObjectItem(payload, "cluster_id");
        const cJSON *attr = cJSON_GetObjectItem(payload, "attribute_id");
        if (!cJSON_IsString(node) || !cJSON_IsNumber(ep) || !cJSON_IsNumber(cluster) || !cJSON_IsNumber(attr)) {
            ESP_LOGW(TAG, "Invalid read-attr payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_attr_read_command(
            static_cast<uint64_t>(node->valueint),
            static_cast<uint16_t>(ep->valueint),
            static_cast<uint32_t>(cluster->valueint),
            static_cast<uint32_t>(attr->valueint));
    }

    // matter.attribute_subscribe
    if (strcmp(action, "matter.attribute_subscribe") == 0) {
        const cJSON *node = cJSON_GetObjectItem(payload, "node_id");
        const cJSON *ep = cJSON_GetObjectItem(payload, "endpoint_id");
        const cJSON *cluster = cJSON_GetObjectItem(payload, "cluster_id");
        const cJSON *attr = cJSON_GetObjectItem(payload, "attribute_id");
        const cJSON *min = cJSON_GetObjectItem(payload, "min_interval");
        const cJSON *max = cJSON_GetObjectItem(payload, "max_interval");
        if (!cJSON_IsString(node) || !cJSON_IsNumber(ep) || !cJSON_IsNumber(cluster) ||
            !cJSON_IsNumber(attr) || !cJSON_IsNumber(min) || !cJSON_IsNumber(max)) {
            ESP_LOGW(TAG, "Invalid subscribe-attr payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_attr_subscribe_command(
            static_cast<uint64_t>(node->valueint),
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
