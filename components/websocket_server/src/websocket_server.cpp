#include "websocket_server.h"
// #include "websocket_client.h"
#include <esp_log.h>
#include <cstdlib>
#include <cstring>
#include <websocket_client.h>

static const char *TAG = "WS_SERVER";
static httpd_handle_t server = nullptr;

#define WEBSOCKET_SERVER_URI "/ws"

// Function pointer for the JSON request handler
static json_request_handler_t json_request_handler_fun = nullptr;

static esp_err_t ws_request_handler(httpd_req_t *request) {

    ESP_LOGI(TAG, "WEBSOCKET_SERVER_URI request received");

    // Get the client socket file descriptor
    const int client_fd = httpd_req_to_sockfd(request);
    ESP_LOGI(TAG, "Client FD: %d", client_fd);

    // Check if the request is a WebSocket upgrade request
    if (request->method == HTTP_GET) {
        // Add the client to the management system
        websocket_client_add(client_fd);
        ESP_LOGI(TAG, "New WebSocket connection, FD: %d", client_fd);
        return ESP_OK;
    }

    // Receive WebSocket frame
    ESP_LOGI(TAG, "Receiving WebSocket frame");
    httpd_ws_frame_t ws_frame = {};
    esp_err_t ret = httpd_ws_recv_frame(request, &ws_frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to retrieve frame length: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "WebSocket frame type: %d, length: %d", ws_frame.type, ws_frame.len);

    // Handle close frame
    ESP_LOGI(TAG, "Handling WebSocket close frame");
    if (ws_frame.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "WebSocket client disconnected, FD: %d", client_fd);
        // websocket_client_remove(client_fd);
        return ESP_OK;
    }
    ESP_LOGI(TAG, "WebSocket frame type: %d, length: %d", ws_frame.type, ws_frame.len);

    // Handle text frame
    ESP_LOGI(TAG, "Handling WebSocket text frame");
    if (ws_frame.type == HTTPD_WS_TYPE_TEXT) {

        // Allocate memory for the incoming message
        ESP_LOGI(TAG, "Allocating memory for WebSocket frame payload");
        auto *buffer = static_cast<uint8_t *>(calloc(1, ws_frame.len + 1));
        if (!buffer) {
            ESP_LOGE(TAG, "Memory allocation failed");
            return ESP_ERR_NO_MEM;
        }
        ws_frame.payload = buffer;

        // Receive the WebSocket frame payload
        ESP_LOGI(TAG, "Receiving WebSocket frame payload");
        ret = httpd_ws_recv_frame(request, &ws_frame, ws_frame.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to receive payload: %s", esp_err_to_name(ret));
            free(buffer);
            return ret;
        }
        ESP_LOGI(TAG, "WebSocket frame payload received, length: %d", ws_frame.len);

        // Call the message handler
        ESP_LOGI(TAG, "Calling JSON request handler");
        char *response_message = nullptr;
        const bool isAuthenticated = websocket_client_is_authenticated(client_fd);
        ret = json_request_handler_fun(reinterpret_cast<char *>(ws_frame.payload), &response_message, isAuthenticated);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "JSON request handler succeeded");

            if (!isAuthenticated) {
                // Authenticate the client
                ESP_LOGI(TAG, "Authenticating the client");
                ret = websocket_client_authenticate(client_fd);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to authenticate client: %s", esp_err_to_name(ret));
                    free(response_message);
                    return ret;
                }

            }
        } else {
            ESP_LOGE(TAG, "Message handler failed: %s", esp_err_to_name(ret));
        }
        ESP_LOGI(TAG, "JSON request handler completed");

        if (response_message) {
            ESP_LOGI(TAG, "Sending response message: %s", response_message);

            // Send the response message back to the client
            websocket_send_message_to_all_clients(response_message);
            free(response_message);
        }
        ESP_LOGI(TAG, "Response message sent");

        // Free the buffer
        free(buffer);

    } else {
        ESP_LOGE(TAG, "Unsupported WebSocket frame type: %d", ws_frame.type);
        return ESP_ERR_INVALID_ARG;
    }

    return ret;
}

esp_err_t websocket_server_start(const json_request_handler_t json_request_handler) {
    ESP_LOGI(TAG, "Starting WebSocket server");
    if (server) {
        ESP_LOGE(TAG, "WebSocket server is already running");
        return ESP_FAIL;
    }

    json_request_handler_fun = json_request_handler;

    // Initialize the WebSocket client management system
    ESP_LOGI(TAG, "Initializing WebSocket client management system");
    websocket_client_init();

    // Start the HTTP server
    ESP_LOGI(TAG, "Starting HTTP server");
    constexpr httpd_config_t server_cfg = HTTPD_DEFAULT_CONFIG();
    esp_err_t ret = httpd_start(&server, &server_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register the WebSocket URI handler
    ESP_LOGI(TAG, "Registering WebSocket URI handler");
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
    ESP_LOGI(TAG, "Stopping WebSocket server");
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
    ESP_LOGI(TAG, "WebSocket server stopped");

    return ret;
}



typedef struct {
    httpd_handle_t server;
    const char* message;
} broadcast_data_t;

static esp_err_t send_message_to_client_callback(websocket_client_t* client, void* arg) {
    broadcast_data_t* data = (broadcast_data_t*)arg;

    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t*)data->message,
        .len = strlen(data->message)
    };

    esp_err_t ret = httpd_ws_send_frame_async(data->server, client->fd, &frame);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send message to client FD %d: %s",
                client->fd, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Message sent to client FD: %d", client->fd);
    return ESP_OK;
}

esp_err_t websocket_send_message_to_all_clients(const char *message) {
    if (!server) {
        ESP_LOGE(TAG, "WebSocket server is not running");
        return ESP_FAIL;
    }

    if (!message) {
        ESP_LOGE(TAG, "Message is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    broadcast_data_t data = {
        .server = server,
        .message = message
    };

    ESP_LOGI(TAG, "Broadcasting message to all clients: %s", message);
    return websocket_client_for_each(send_message_to_client_callback, &data);
}