#include "json_request_handler.h"

#include <cJSON.h>
#include <esp_log.h>
#include <cstring>

static const char *TAG = "JSON_PARSER";
static const char *AUTH_TOKEN = "secret_token_123";

static esp_err_t handle_command(const cJSON *root, char **response_message) {
    if (!root) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *response = cJSON_CreateObject();

    // Check for the presence of the command in the request
    cJSON *command = cJSON_GetObjectItem(root, "command");
    if (!cJSON_IsString(command)) {
        ESP_LOGE(TAG, "Missing or invalid 'command' in the payload");
        cJSON_AddStringToObject(response, "command", "error");
        cJSON_AddStringToObject(response, "payload", "Invalid command");
        return ESP_FAIL;
    }

    // init_thread_network command
    if (strcmp(command->valuestring, "init_thread_network") == 0) {
        cJSON *payload = cJSON_GetObjectItem(root, "payload");
        if (cJSON_IsString(payload)) {
            ESP_LOGI(TAG, "Received payload: %s", payload->valuestring);
        } else {
            ESP_LOGE(TAG, "Invalid payload format");
            cJSON_AddStringToObject(response, "command", "error");
            cJSON_AddStringToObject(response, "payload", "Invalid payload format");

            *response_message = cJSON_PrintUnformatted(response);
            return ESP_FAIL;
        }

        const char *dataset = "Simulated Dataset TLVs";
        if (dataset) {
            ESP_LOGI(TAG, "Generated Dataset TLVs: %s", dataset);
            cJSON_AddStringToObject(response, "command", "init_thread_network");
            cJSON_AddStringToObject(response, "payload", dataset);
        } else {
            cJSON_AddStringToObject(response, "command", "error");
            cJSON_AddStringToObject(response, "payload", "Failed to generate dataset");
            ESP_LOGE(TAG, "Failed to generate dataset TLVs.");
        }
    } else {
        ESP_LOGE(TAG, "Unknown command: %s", command->valuestring);
        cJSON_AddStringToObject(response, "command", "error");
        cJSON_AddStringToObject(response, "payload", "Unknown command");
    }

    *response_message = cJSON_PrintUnformatted(response);
    return ESP_OK;
}

static esp_err_t handle_unauthenticated_request(const cJSON *root, char **response_message) {
    ESP_LOGI(TAG, "Received unauthenticated request");

    // Validate the arguments
    if (!root) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *response = cJSON_CreateObject();

    ESP_LOGI(TAG, "Parsing token");
    // Check for the presence of the token in the request and validate it
    cJSON *token = cJSON_GetObjectItem(root, "token");
    if (cJSON_IsString(token) && strcmp(token->valuestring, AUTH_TOKEN) == 0) {
        ESP_LOGI(TAG, "Authentication successful");
        cJSON_AddStringToObject(response, "command", "auth");
        cJSON_AddStringToObject(response, "payload", "success");
        *response_message = cJSON_PrintUnformatted(response);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Authentication failed");
    cJSON_AddStringToObject(response, "command", "error");
    cJSON_AddStringToObject(response, "payload", "Invalid token");
    *response_message = cJSON_PrintUnformatted(response);
    return ESP_ERR_INVALID_STATE;
}

esp_err_t handle_request(const char *request_message, char **response_message, const bool isAuthenticated) {
    ESP_LOGI(TAG, "Preparing to parse WebSocket request");

    if (!request_message || !response_message) {
        ESP_LOGE(TAG, "Invalid arguments, request_message or response_message is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Parsing WebSocket request: %s", request_message);
    cJSON *root = cJSON_Parse(request_message);
    if (!root) {
        const char *error = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "Parse error near: %s", error ? error : "unknown");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Parsed JSON: %s", cJSON_Print(root));

    const esp_err_t ret = (isAuthenticated)
                              ? handle_command(root, response_message)
                              : handle_unauthenticated_request(root, response_message);

    cJSON_Delete(root);

    return ret;
}
