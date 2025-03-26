#include "websocket_server.h"
#include "websocket_client.h"
#include <esp_log.h>
#include <cstdlib>
#include <cstring>

static const char *TAG = "WS_SERVER";
static httpd_handle_t server = nullptr;

#define WEBSOCKET_SERVER_URI "/ws"

// Function pointer for the JSON request handler
static json_request_handler_t json_request_handler_fun = nullptr;

static esp_err_t ws_request_handler(httpd_req_t *request) {

    // Get the client socket file descriptor
    const int client_fd = httpd_req_to_sockfd(request);

    // Check if the request is a WebSocket upgrade request
    if (request->method == HTTP_GET) {
        // Add the client to the management system
        websocket_client_add(client_fd);
        ESP_LOGI(TAG, "New WebSocket connection, FD: %d", client_fd);
        return ESP_OK;
    }

    // Receive WebSocket frame
    httpd_ws_frame_t ws_frame = {};
    esp_err_t ret = httpd_ws_recv_frame(request, &ws_frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to retrieve frame length: %s", esp_err_to_name(ret));
        return ret;
    }

    // Handle close frame
    if (ws_frame.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "WebSocket client disconnected, FD: %d", client_fd);
        websocket_client_remove(client_fd);
        return ESP_OK;
    }

    // Handle text frame
    if (ws_frame.type == HTTPD_WS_TYPE_TEXT) {
        // Allocate memory for the incoming message
        auto *buffer = static_cast<uint8_t *>(calloc(1, ws_frame.len + 1));
        if (!buffer) {
            ESP_LOGE(TAG, "Memory allocation failed");
            return ESP_ERR_NO_MEM;
        }
        ws_frame.payload = buffer;

        // Receive the WebSocket frame payload
        ret = httpd_ws_recv_frame(request, &ws_frame, ws_frame.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to receive payload: %s", esp_err_to_name(ret));
            free(buffer);
            return ret;
        }

        // Call the message handler
        char *response_message = nullptr;
        const bool isAuthenticated = websocket_client_is_authenticated(client_fd);
        ret = json_request_handler_fun(reinterpret_cast<char *>(ws_frame.payload), &response_message, isAuthenticated);
        if (ret == ESP_OK) {
            if (!isAuthenticated) {
                // Authenticate the client
                websocket_client_authenticate(client_fd);
            }
        } else {
            ESP_LOGE(TAG, "Message handler failed: %s", esp_err_to_name(ret));
        }

        if (response_message) {
            // Send the response message back to the client
            websocket_send_message(client_fd, response_message);
            free(response_message);
        }

        // Free the buffer
        free(buffer);

    } else {
        ESP_LOGE(TAG, "Unsupported WebSocket frame type: %d", ws_frame.type);
        return ESP_ERR_INVALID_ARG;
    }

    return ret;
}

esp_err_t websocket_server_start(const json_request_handler_t json_request_handler) {
    if (server) {
        ESP_LOGE(TAG, "WebSocket server is already running");
        return ESP_FAIL;
    }

    json_request_handler_fun = json_request_handler;

    // Initialize the WebSocket client management system
    websocket_client_init();

    // Start the HTTP server
    constexpr httpd_config_t server_cfg = HTTPD_DEFAULT_CONFIG();
    esp_err_t ret = httpd_start(&server, &server_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register the WebSocket URI handler
    constexpr httpd_uri_t ws_uri = {
        .uri = WEBSOCKET_SERVER_URI,
        .method = HTTP_GET,
        .handler = ws_request_handler,
        .user_ctx = nullptr,
        // .user_ctx = const_cast<websocket_server_config_t *>(config),
        .is_websocket = true
    };
    ret = httpd_register_uri_handler(server, &ws_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WebSocket URI: %s", esp_err_to_name(ret));
        httpd_stop(server);
        server = nullptr;
        return ret;
    }

    ESP_LOGI(TAG, "WebSocket server started on URI: %s", WEBSOCKET_SERVER_URI);
    return ESP_OK;
}

esp_err_t websocket_server_stop() {
    if (!server) {
        ESP_LOGE(TAG, "WebSocket server is not running");
        return ESP_FAIL;
    }

    // Stop the HTTP server
    const esp_err_t ret = httpd_stop(server);
    if (ret == ESP_OK) {
        server = nullptr;
        ESP_LOGI(TAG, "WebSocket server stopped");
    } else {
        ESP_LOGE(TAG, "Failed to stop WebSocket server: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t websocket_send_message(const int client_fd, const char *message) {
    if (!server) {
        ESP_LOGE(TAG, "WebSocket server is not running");
        return ESP_FAIL;
    }

    // Prepare the WebSocket frame
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = reinterpret_cast<uint8_t *>(const_cast<char *>(message)),
        .len = strlen(message)
    };

    // Send the WebSocket frame asynchronously
    const esp_err_t ret = httpd_ws_send_frame_async(server, client_fd, &frame);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send WebSocket message: %s", esp_err_to_name(ret));
    }

    return ret;
}