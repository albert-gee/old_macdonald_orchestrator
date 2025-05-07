#include "../../include/messages/json_inbound_message.h"
#include "sdkconfig.h"
#include "commands/commands.h"

#include <cJSON.h>
#include <esp_event.h>
#include <esp_log.h>
#include <cstring>

static const char *TAG = "JSON_INBOUND_HANDLER";

static bool parse_uint64(const char *s, uint64_t *out) {
    if (!s || !*s) return false;
    char *end;
    const uint64_t val = strtoull(s, &end, 0);
    if (end == s || *end != '\0') return false;
    *out = val;
    return true;
}

static bool parse_uint32(const char *s, uint32_t *out) {
    if (!s || !*s) return false;
    char *end;
    const unsigned long val = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || val > UINT32_MAX) return false;
    *out = static_cast<uint32_t>(val);
    return true;
}

static bool parse_uint16(const char *s, uint16_t *out) {
    if (!s || !*s) return false;
    char *end;
    const unsigned long val = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || val > UINT16_MAX) return false;
    *out = static_cast<uint16_t>(val);
    return true;
}

static esp_err_t process_command_message(const char *action, const cJSON *payload) {
    if (strcmp(action, "wifi.sta_connect") == 0) {
        const cJSON *ssid = cJSON_GetObjectItem(payload, "ssid");
        const cJSON *password = cJSON_GetObjectItem(payload, "password");
        if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) {
            ESP_LOGW(TAG, "Invalid Wi-Fi payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_wifi_sta_connect_command(ssid->valuestring, password->valuestring);
    }

    if (strcmp(action, "thread.init") == 0) {
        return execute_thread_network_init_command(
            cJSON_GetObjectItem(payload, "channel")->valueint,
            cJSON_GetObjectItem(payload, "pan_id")->valueint,
            cJSON_GetObjectItem(payload, "network_name")->valuestring,
            cJSON_GetObjectItem(payload, "extended_pan_id")->valuestring,
            cJSON_GetObjectItem(payload, "mesh_local_prefix")->valuestring,
            cJSON_GetObjectItem(payload, "master_key")->valuestring,
            cJSON_GetObjectItem(payload, "pskc")->valuestring
        );
    }

    if (strcmp(action, "thread.start") == 0) {
        return execute_thread_start_command();
    }

    if (strcmp(action, "ifconfig.up") == 0) {
        return execute_ifconfig_up_command();
    }

    if (strcmp(action, "pair.ble_thread") == 0) {
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

        return execute_pair_ble_thread_command(node_id_val, pin, disc);
    }

    if (strcmp(action, "cluster_command.invoke") == 0) {
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


        return execute_invoke_cmd_command(static_cast<uint64_t>(dest->valueint),
                                              static_cast<uint16_t>(ep->valueint),
                                              static_cast<uint32_t>(cluster->valueint),
                                              static_cast<uint32_t>(cmd->valueint),
                                              data->valuestring);
    }

    if (strcmp(action, "attribute.read") == 0) {
        const cJSON *node = cJSON_GetObjectItem(payload, "node_id");
        const cJSON *ep = cJSON_GetObjectItem(payload, "endpoint_id");
        const cJSON *cluster = cJSON_GetObjectItem(payload, "cluster_id");
        const cJSON *attr = cJSON_GetObjectItem(payload, "attribute_id");
        if (!cJSON_IsString(node) || !cJSON_IsNumber(ep) || !cJSON_IsNumber(cluster) || !cJSON_IsNumber(attr)) {
            ESP_LOGW(TAG, "Invalid read-attr payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_read_attr_command(
            static_cast<uint64_t>(node->valueint),
            static_cast<uint16_t>(ep->valueint),
            static_cast<uint32_t>(cluster->valueint),
            static_cast<uint32_t>(attr->valueint));
    }

    if (strcmp(action, "attribute.subscribe") == 0) {
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

        return execute_subscribe_attr_command(
            static_cast<uint64_t>(node->valueint),
            static_cast<uint16_t>(ep->valueint),
            static_cast<uint32_t>(cluster->valueint),
            static_cast<uint32_t>(attr->valueint),
            static_cast<uint16_t>(min->valueint),
            static_cast<uint16_t>(max->valueint)
        );
    }

    if (strcmp(action, "matter_controller.init") == 0) {
        const cJSON *node_id = cJSON_GetObjectItem(payload, "node_id");
        const cJSON *fabric_id = cJSON_GetObjectItem(payload, "fabric_id");
        const cJSON *listen_port = cJSON_GetObjectItem(payload, "listen_port");
        if (!cJSON_IsString(node_id) || !cJSON_IsNumber(fabric_id) || !cJSON_IsNumber(listen_port)) {
            ESP_LOGW(TAG, "Invalid Matter init payload");
            return ESP_ERR_INVALID_ARG;
        }

        return execute_init_matter_controller_command(
            static_cast<uint64_t>(node_id->valueint),
            static_cast<uint64_t>(fabric_id->valuedouble),
            static_cast<uint16_t>(listen_port->valueint)
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

    if (strcmp(type->valuestring, "command") == 0) {
        ret = process_command_message(action->valuestring, payload);
    } else {
        ESP_LOGW(TAG, "Unsupported message type");
    }

    cJSON_Delete(root);
    return ret;
}
