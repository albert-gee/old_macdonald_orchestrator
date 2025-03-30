#include "json/json_request_handler.h"
#include "json/json_response.h"

#include <cJSON.h>
#include <esp_log.h>
#include <cstring>
#include <thread_util.h>

static const char *TAG = "JSON_PARSER";
static const char *AUTH_TOKEN = "secret_token_123";

static esp_err_t handle_command_init_thread_network(const cJSON *root, char **response_message) {
    ESP_LOGI(TAG, "init_thread_network command received");

    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    if (!cJSON_IsObject(payload)) {
        *response_message = create_json_error_response("Invalid payload format");
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
        *response_message = create_json_error_response("Invalid or missing fields in payload");
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
        *response_message = create_json_error_response("Failed to initialize Thread dataset");
        return err;
    }
    ESP_LOGI(TAG, "Thread dataset initialized successfully");

    *response_message = create_json_info_response("Thread dataset initialized successfully");

    return ESP_OK;
}

/**
 * @brief Handles authenticated commands.
 */
static esp_err_t handle_command(const cJSON *root, char **response_message) {
    if (!root) {
        ESP_LOGE(TAG, "Invalid JSON root object");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Received a command");


    // Check for "command" field
    cJSON *command = cJSON_GetObjectItem(root, "command");
    if (!command || !cJSON_IsString(command)) {
        *response_message = create_json_error_response("Invalid command format");
        return ESP_FAIL;
    }

    // Handle "init_thread_network" command
    if (strcmp(command->valuestring, "init_thread_network") == 0) {
        handle_command_init_thread_network(root, response_message);
    // Unknown command
    } else {
        *response_message = create_json_error_response("Unknown command");
    }

    return ESP_OK;
}


static esp_err_t handle_unauthenticated_request(const cJSON *root, char **response_message) {
    ESP_LOGI(TAG, "Received unauthenticated request");

    // Validate the arguments
    if (!root) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    // Check for "command" field
    cJSON *command = cJSON_GetObjectItem(root, "command");
    if (!command || !cJSON_IsString(command)) {
        *response_message = create_json_error_response("Invalid command format");
        ESP_LOGE(TAG, "Invalid command format");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Command: %s", command->valuestring);

    // Command is "auth"
    if (strcmp(command->valuestring, "auth") == 0) {
        cJSON *code = cJSON_GetObjectItem(root, "payload");
        if (cJSON_IsString(code) && strcmp(code->valuestring, AUTH_TOKEN) == 0) {
            *response_message = create_json_info_response("Authenticated successfully");
            ESP_LOGI(TAG, "AUTH token matched");
            return ESP_OK;
        }
        *response_message = create_json_error_response("Invalid auth code");
        ESP_LOGE(TAG, "Invalid auth code");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGE(TAG, "Invalid command: %s", command->valuestring);

    *response_message = create_json_error_response("Unauthenticated. Invalid command");
    return ESP_ERR_INVALID_STATE;
}

/**
 * @brief Handles WebSocket JSON requests.
 */
esp_err_t handle_json_request(char *request_message, char **response_message, bool isAuthenticated) {
    if (!request_message || !response_message) {
        ESP_LOGE(TAG, "Invalid arguments, request_message or response_message is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Received request: %s", request_message);

    cJSON *root = cJSON_Parse(request_message);
    if (!root) {
        ESP_LOGE(TAG, "JSON parsing failed");
        *response_message = create_json_error_response("Invalid JSON format");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Parsed JSON successfully");

    const esp_err_t ret = (isAuthenticated)
                              ? handle_command(root, response_message)
                              : handle_unauthenticated_request(root, response_message);

    cJSON_Delete(root); // Free the parsed JSON object
    return ret;
}
