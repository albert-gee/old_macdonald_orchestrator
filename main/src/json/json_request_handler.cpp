#include "json/json_request_handler.h"

#include <cJSON.h>
#include <esp_log.h>
#include <cstring>
#include <thread_util.h>
#include <wifi_interface.h>
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
    ESP_LOGI(TAG, "Extracted values from payload: ssid=%s, password=%s", ssid->valuestring, password->valuestring);

    // Validate required parameters
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) {
        broadcast_error_message("Invalid or missing fields in payload");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Validated required parameters");

    // Call wifi_connect_sta_to_ap
    esp_err_t err = wifi_connect_sta_to_ap(ssid->valuestring, password->valuestring);
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
    ESP_LOGI(TAG, "Extracted values from payload: channel=%d, pan_id=%d, network_name=%s, extended_pan_id=%s, mesh_local_prefix=%s, master_key=%s, pskc=%s",
             channel->valueint, pan_id->valueint, network_name->valuestring,
             extended_pan_id->valuestring, mesh_local_prefix->valuestring,
             master_key->valuestring, pskc->valuestring);

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

static esp_err_t handle_command(const cJSON *root) {
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
    } else {
        broadcast_error_message("Unknown command");
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
    // if (!request_message || !response_message) {
    //     ESP_LOGE(TAG, "Invalid arguments, request_message or response_message is NULL");
    //     return ESP_ERR_INVALID_ARG;
    // }
    // ESP_LOGI(TAG, "Received request: %s", request_message);

    cJSON *root = cJSON_Parse(request_message);
    if (!root) {
        ESP_LOGE(TAG, "JSON parsing failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Parsed JSON successfully");

    const esp_err_t ret = (isAuthenticated)
                              ? handle_command(root)
                              : handle_unauthenticated_request(root);

    cJSON_Delete(root); // Free the parsed JSON object
    return ret;
}
