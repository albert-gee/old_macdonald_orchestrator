#include "json/json_request_handler.h"

#include <cJSON.h>
#include <esp_log.h>
#include <cstring>
#include <matter_util.h>
#include <thread_util.h>
#include <wifi_interface.h>
#include <app/ConcreteAttributePath.h>
#include <core/TLVReader.h>
#include <events/chip_event_handler.h>
#include <utils/event_handler_util.h>

static const char *TAG = "JSON_REQUEST_HANDLER";
static const char *AUTH_TOKEN = "secret_token_123";

static esp_err_t handle_command_wifi_sta_connect(const cJSON *root) {
    ESP_LOGI(TAG, "wifi_sta_connect command received");

    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    if (!cJSON_IsObject(payload)) {
        broadcast_error_message("Invalid payload format");
        return ESP_FAIL;
    }

    // Extract values
    const cJSON *ssid = cJSON_GetObjectItem(payload, "ssid");
    const cJSON *password = cJSON_GetObjectItem(payload, "password");

    // Validate required parameters
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) {
        broadcast_error_message("Invalid or missing fields in payload");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Extracted values from payload: ssid=%s, password=%s", ssid->valuestring, password->valuestring);

    // Call wifi_connect_sta_to_ap
    esp_err_t err = wifi_sta_connect(ssid->valuestring, password->valuestring);
    if (err != ESP_OK) {
        broadcast_error_message("Failed to connect to Wi-Fi AP");
        return err;
    }
    ESP_LOGI(TAG, "Connected to Wi-Fi AP successfully");

    broadcast_info_message("Connected to Wi-Fi AP successfully");

    return ESP_OK;
}

static esp_err_t handle_command_init_thread_network(const cJSON *root) {
    ESP_LOGI(TAG, "init_thread_network command received");

    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    if (!cJSON_IsObject(payload)) {
        broadcast_error_message("Invalid payload format");
        return ESP_FAIL;
    }

    // Extract values
    const cJSON *channel = cJSON_GetObjectItem(payload, "channel");
    const cJSON *pan_id = cJSON_GetObjectItem(payload, "pan_id");
    const cJSON *network_name = cJSON_GetObjectItem(payload, "network_name");
    const cJSON *extended_pan_id = cJSON_GetObjectItem(payload, "extended_pan_id");
    const cJSON *mesh_local_prefix = cJSON_GetObjectItem(payload, "mesh_local_prefix");
    const cJSON *master_key = cJSON_GetObjectItem(payload, "master_key");
    const cJSON *pskc = cJSON_GetObjectItem(payload, "pskc");
    ESP_LOGI(TAG, "Extracted values from payload");

    // Validate required parameters
    if (!cJSON_IsNumber(channel) || !cJSON_IsNumber(pan_id) ||
        !cJSON_IsString(network_name) || !cJSON_IsString(extended_pan_id) ||
        !cJSON_IsString(mesh_local_prefix) || !cJSON_IsString(master_key) ||
        !cJSON_IsString(pskc)) {
        broadcast_error_message("Invalid or missing fields in payload");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Validated required parameters");

    // Call thread_dataset_init
    esp_err_t err = thread_dataset_init(
        (uint16_t)channel->valueint,
        (uint16_t)pan_id->valueint,
        network_name->valuestring,
        extended_pan_id->valuestring,
        mesh_local_prefix->valuestring,
        master_key->valuestring,
        pskc->valuestring
    );
    if (err != ESP_OK) {
        broadcast_error_message("Failed to initialize Thread dataset");
        return err;
    }
    ESP_LOGI(TAG, "Thread dataset initialized successfully");

    broadcast_info_message("Thread dataset initialized successfully");

    return ESP_OK;
}

static esp_err_t handle_command_ifconfig_up(const cJSON *root) {
    ESP_LOGI(TAG, "ifconfig_up command received");

    esp_err_t err = ifconfig_up();
    if (err != ESP_OK) {
        broadcast_error_message("Failed to bring up Thread interface");
        return err;
    }
    ESP_LOGI(TAG, "Thread interface brought up successfully");

    broadcast_info_message("Thread interface brought up successfully");

    return ESP_OK;
}

static esp_err_t handle_command_thread_start(const cJSON *root) {
    ESP_LOGI(TAG, "thread_start command received");

    esp_err_t err = thread_start();
    if (err != ESP_OK) {
        broadcast_error_message("Failed to start Thread stack");
        return err;
    }
    ESP_LOGI(TAG, "Thread stack started successfully");

    broadcast_info_message("Thread stack started successfully");

    return ESP_OK;
}

static esp_err_t handle_command_pair_ble_thread(const cJSON *root) {
    ESP_LOGI(TAG, "pair_ble_thread command received");

    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    if (!cJSON_IsObject(payload)) {
        broadcast_error_message("Invalid payload format");
        return ESP_FAIL;
    }

    // Extract values from JSON
    const cJSON *node_id = cJSON_GetObjectItem(payload, "node_id");
    const cJSON *setup_code = cJSON_GetObjectItem(payload, "setup_code");
    const cJSON *discriminator = cJSON_GetObjectItem(payload, "discriminator");

    // Validate required parameters
    if (!cJSON_IsString(node_id) || !cJSON_IsString(setup_code) || !cJSON_IsString(discriminator)) {
        broadcast_error_message("Invalid or missing fields in payload");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Extracted values from payload: node_id=%s, setup_code=%s, discriminator=%s",
             node_id->valuestring, setup_code->valuestring, discriminator->valuestring);

    // Parse and validate node_id, setup_code, and discriminator
    errno = 0;
    const uint64_t node_id_val = strtoull(node_id->valuestring, nullptr, 0);
    if (errno != 0 || node_id_val == 0) {
        ESP_LOGE(TAG, "Invalid node_id format");
        return ESP_ERR_INVALID_ARG;
    }

    errno = 0;
    uint32_t pin_val = strtoul(setup_code->valuestring, nullptr, 10);
    if (errno != 0 || pin_val == 0) {
        ESP_LOGE(TAG, "Invalid pin format");
        return ESP_ERR_INVALID_ARG;
    }

    errno = 0;
    uint16_t discriminator_val = strtoul(discriminator->valuestring, nullptr, 10);
    if (errno != 0 || discriminator_val == 0) {
        ESP_LOGE(TAG, "Invalid discriminator format");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Parsed values: node_id=0x%" PRIX64 ", pin=%" PRIu32 ", discriminator=%" PRIu16,
             node_id_val, pin_val, discriminator_val);

    // Get Thread dataset in TLV format
    uint8_t dataset_tlvs[OT_OPERATIONAL_DATASET_MAX_LENGTH];
    uint8_t dataset_len = sizeof(dataset_tlvs);

    esp_err_t err = thread_get_active_dataset_tlvs(dataset_tlvs, &dataset_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get Thread dataset TLVs");
        return err;
    }
    ESP_LOGI(TAG, "Thread dataset length: %zu", dataset_len);

    // Call pairing function with extracted values
    err = pairing_ble_thread(node_id_val, pin_val, discriminator_val, dataset_tlvs, dataset_len);
    if (err != ESP_OK) {
        broadcast_error_message("Failed to pair with Thread device");
        return err;
    }

    ESP_LOGI(TAG, "Thread-BLE pairing initiated successfully");
    broadcast_info_message("Thread-BLE pairing initiated successfully");
    return ESP_OK;
}

// Static function to handle invoking the cluster command
static esp_err_t handle_command_invoke_cluster_command(const cJSON *root) {
    ESP_LOGI(TAG, "invoke_cluster_command received");

    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    if (!cJSON_IsObject(payload)) {
        broadcast_error_message("Invalid payload format");
        return ESP_FAIL;
    }

    // Extract values for the invoke cluster command
    const cJSON *destination_id = cJSON_GetObjectItem(payload, "destination_id");
    const cJSON *endpoint_id = cJSON_GetObjectItem(payload, "endpoint_id");
    const cJSON *cluster_id = cJSON_GetObjectItem(payload, "cluster_id");
    const cJSON *command_id = cJSON_GetObjectItem(payload, "command_id");
    const cJSON *command_data_field = cJSON_GetObjectItem(payload, "command_data_field");

    // Validate required parameters
    if (!cJSON_IsString(destination_id) || !cJSON_IsNumber(endpoint_id) ||
        !cJSON_IsNumber(cluster_id) || !cJSON_IsNumber(command_id) || !cJSON_IsString(command_data_field)) {
        broadcast_error_message("Invalid or missing fields in payload");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Extracted values from payload: destination_id=%s, endpoint_id=%d, cluster_id=%d, command_id=%d, command_data_field=%s",
             destination_id->valuestring, endpoint_id->valueint, cluster_id->valueint,
             command_id->valueint, command_data_field->valuestring);

    uint64_t node_id1 = strtoull(destination_id->valuestring, nullptr, 0);
    auto endpoint_id1 = static_cast<uint16_t>(endpoint_id->valueint);
    auto cluster_id1 = static_cast<uint32_t>(cluster_id->valueint);
    auto command_id1 = static_cast<uint32_t>(command_id->valueint);
    const char *command_data_field1 = command_data_field->valuestring;

    esp_err_t err = invoke_cluster_command(
        node_id1,
        endpoint_id1,
        cluster_id1, command_id1,
        command_data_field1
    );
    if (err != ESP_OK) {
        broadcast_error_message("Failed to invoke cluster command");
        return err;
    }

    ESP_LOGI(TAG, "Cluster command invoked successfully");
    broadcast_info_message("Cluster command invoked successfully");

    return ESP_OK;
}

static esp_err_t handle_command_read_attr_command(const cJSON *root) {
    ESP_LOGI(TAG, "read_attr_command received");

    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    if (!cJSON_IsObject(payload)) {
        broadcast_error_message("Invalid payload format");
        return ESP_FAIL;
    }

    // Extract values for the invoke cluster command
    const cJSON *node_id = cJSON_GetObjectItem(payload, "node_id");
    const cJSON *endpoint_id = cJSON_GetObjectItem(payload, "endpoint_id");
    const cJSON *cluster_id = cJSON_GetObjectItem(payload, "cluster_id");
    const cJSON *attribute_id = cJSON_GetObjectItem(payload, "attribute_id");

    // Validate required parameters
    if (!cJSON_IsString(node_id) || !cJSON_IsNumber(endpoint_id) ||
        !cJSON_IsNumber(cluster_id) || !cJSON_IsNumber(attribute_id)) {
        broadcast_error_message("Invalid or missing fields in payload");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Extracted values from payload: node_id=%s, endpoint_id=%d, cluster_id=%d, attribute_id=%d",
             node_id->valuestring, endpoint_id->valueint, cluster_id->valueint,
             attribute_id->valueint);

    // Call the invoke cluster command
    esp_err_t err = send_read_attr_command(
        strtoull(node_id->valuestring, nullptr, 0),  // Convert destination_id to uint64_t
        static_cast<uint16_t>(endpoint_id->valueint),
        static_cast<uint32_t>(cluster_id->valueint),
        static_cast<uint32_t>(attribute_id->valueint),
        read_attribute_data_callback
    );
    if (err != ESP_OK) {
        broadcast_error_message("Failed to invoke read_attr_command");
        return err;
    }

    ESP_LOGI(TAG, "read_attr_command invoked successfully");
    broadcast_info_message("read_attr_command invoked successfully");

    return ESP_OK;
}

static esp_err_t handle_command_subscribe_attr_command(const cJSON *root) {
    ESP_LOGI(TAG, "subscribe_attr_command received");

    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    if (!cJSON_IsObject(payload)) {
        broadcast_error_message("Invalid payload format");
        return ESP_FAIL;
    }

    // Extract values for the invoke cluster command
    const cJSON *node_id = cJSON_GetObjectItem(payload, "node_id");
    const cJSON *endpoint_id = cJSON_GetObjectItem(payload, "endpoint_id");
    const cJSON *cluster_id = cJSON_GetObjectItem(payload, "cluster_id");
    const cJSON *attribute_id = cJSON_GetObjectItem(payload, "attribute_id");
    const cJSON *min_interval = cJSON_GetObjectItem(payload, "min_interval");
    const cJSON *max_interval = cJSON_GetObjectItem(payload, "max_interval");

    // Validate required parameters
    if (!cJSON_IsString(node_id) || !cJSON_IsNumber(endpoint_id) ||
        !cJSON_IsNumber(cluster_id) || !cJSON_IsNumber(attribute_id) ||
        !cJSON_IsNumber(min_interval) || !cJSON_IsNumber(max_interval)) {
        broadcast_error_message("Invalid or missing fields in payload");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Extracted values from payload: node_id=%s, endpoint_id=%d, cluster_id=%d, attribute_id=%d, min_interval=%d, max_interval=%d",
             node_id->valuestring, endpoint_id->valueint, cluster_id->valueint,
             attribute_id->valueint, min_interval->valueint, max_interval->valueint);

    // Call the invoke cluster command
    esp_err_t err = send_subscribe_attr_command(
        strtoull(node_id->valuestring, nullptr, 0),  // Convert node_id to uint64_t
        static_cast<uint16_t>(endpoint_id->valueint),
        static_cast<uint32_t>(cluster_id->valueint),
        static_cast<uint32_t>(attribute_id->valueint),
        static_cast<uint16_t>(min_interval->valueint),
        static_cast<uint16_t>(max_interval->valueint),
        true,
        subscription_attribute_report_callback,
        subscribe_done_callback
    );
    if (err != ESP_OK) {
        broadcast_error_message("Failed to invoke subscribe_attr_command");
        return err;
    }


    ESP_LOGI(TAG, "subscribe_attr_command invoked successfully");
    broadcast_info_message("subscribe_attr_command invoked successfully");

    return ESP_OK;
}

static esp_err_t handle_authenticated_request(const cJSON *root) {
    if (!root) {
        ESP_LOGE(TAG, "Invalid JSON root object");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Received a command");

    // Check for "command" field
    cJSON *command = cJSON_GetObjectItem(root, "command");
    if (!command || !cJSON_IsString(command)) {
        broadcast_error_message("Invalid command format");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Command: %s", command->valuestring);

    // Handle "init_thread_network" command
    if (strcmp(command->valuestring, "init_thread_network") == 0) {
        handle_command_init_thread_network(root);
    } else if (strcmp(command->valuestring, "wifi_sta_connect") == 0) {
        handle_command_wifi_sta_connect(root);
    } else if (strcmp(command->valuestring, "ifconfig_up") == 0) {
        handle_command_ifconfig_up(root);
    } else if (strcmp(command->valuestring, "thread_start") == 0) {
        handle_command_thread_start(root);
    }  else if (strcmp(command->valuestring, "pair_ble_thread") == 0) {
        handle_command_pair_ble_thread(root);
    } else if (strcmp(command->valuestring, "invoke_cluster_command") == 0) {
        handle_command_invoke_cluster_command(root);
    }  else if (strcmp(command->valuestring, "send_read_attr_command") == 0) {
        handle_command_read_attr_command(root);
    }  else if (strcmp(command->valuestring, "send_subscribe_attr_command") == 0) {
        handle_command_subscribe_attr_command(root);
    } else {
        broadcast_error_message("Unknown command");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static esp_err_t handle_unauthenticated_request(const cJSON *root) {
    ESP_LOGI(TAG, "Received unauthenticated request");

    // Validate the arguments
    if (!root) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    // Check for "command" field
    cJSON *command = cJSON_GetObjectItem(root, "command");
    if (!command || !cJSON_IsString(command)) {
        ESP_LOGE(TAG, "Invalid command format");
        broadcast_error_message("Invalid command format");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Command: %s", command->valuestring);

    // Command is "auth"
    if (strcmp(command->valuestring, "auth") == 0) {
        cJSON *code = cJSON_GetObjectItem(root, "payload");
        if (cJSON_IsString(code) && strcmp(code->valuestring, AUTH_TOKEN) == 0) {
            ESP_LOGI(TAG, "AUTH token matched");
            broadcast_info_message("Authenticated successfully");
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Invalid auth code");
        broadcast_error_message("Invalid command format");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGE(TAG, "Invalid command: %s", command->valuestring);
    broadcast_error_message("Unauthenticated. Invalid command");
    return ESP_ERR_INVALID_STATE;
}

esp_err_t handle_json_request(char *request_message, bool isAuthenticated) {

    cJSON *root = cJSON_Parse(request_message);
    if (!root) {
        ESP_LOGE(TAG, "JSON parsing failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Parsed JSON successfully");

    esp_err_t ret = ESP_FAIL;
    if (isAuthenticated) {
        ret = handle_authenticated_request(root);
    } else {
        ret = handle_unauthenticated_request(root);
    }

    cJSON_Delete(root);
    return ret;
}
