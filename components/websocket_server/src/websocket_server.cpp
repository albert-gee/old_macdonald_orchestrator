#include "websocket_server.h"

#include <esp_log.h>
#include <cstdlib>
#include <cstring>

static const char *TAG = "WS_SERVER";
static httpd_handle_t server = nullptr; // HTTP Server Instance Handle

LIST_HEAD(client_list, websocket_client);
client_list clients = LIST_HEAD_INITIALIZER(clients);


#define WEBSOCKET_SERVER_URI "/ws"

// Function pointer for the JSON request handler
static ws_inbound_message_handler_t inbound_message_handler_fun = nullptr;

static void websocket_client_add(int fd) {
    websocket_client_t *c;

    LIST_FOREACH(c, &clients, entries) {
        if (c->fd == fd) return;  // Already in list
    }

    c = (websocket_client_t *)malloc(sizeof(websocket_client_t));
    if (!c) {
        ESP_LOGE(TAG, "Failed to allocate memory for new client");
        return;
    }

    c->fd = fd;
    LIST_INSERT_HEAD(&clients, c, entries);
    ESP_LOGI(TAG, "Client %d added", fd);
}

static void websocket_client_remove(int fd) {
    websocket_client_t *c;

    LIST_FOREACH(c, &clients, entries) {
        if (c->fd == fd) {
            LIST_REMOVE(c, entries);
            free(c);
            ESP_LOGI(TAG, "Client %d removed", fd);
            return;
        }
    }

    ESP_LOGW(TAG, "Client %d not found for removal", fd);
}

static void websocket_client_cleanup() {
    websocket_client_t *c, *tmp;
    for (c = LIST_FIRST(&clients); c != NULL;) {
        tmp = LIST_NEXT(c, entries);
        LIST_REMOVE(c, entries);
        free(c);
        c = tmp;
    }
}

static esp_err_t inbound_message_handler(httpd_req_t *request) {

    ESP_LOGI(TAG, "Websocket message received");

    esp_err_t ret = ESP_OK;


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
    ret = httpd_ws_recv_frame(request, &ws_frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to retrieve frame length: %s", esp_err_to_name(ret));
        return ret;
    }

    // Handle WS Close frame
    if (ws_frame.type == HTTPD_WS_TYPE_CLOSE) {
        websocket_client_remove(client_fd);
        ESP_LOGI(TAG, "Client %d disconnected", client_fd);
        return ESP_OK;
    }

    // Handle text frame
    if (ws_frame.type == HTTPD_WS_TYPE_TEXT) {
        ESP_LOGI(TAG, "Handling WebSocket text frame");

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

        // Call the JSON message handler
        ESP_LOGI(TAG, "Calling JSON request handler");
        ret = inbound_message_handler_fun(reinterpret_cast<char *>(ws_frame.payload));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Message handler failed: %s", esp_err_to_name(ret));
        }

        // Free the buffer
        free(buffer);

    }

    return ret;
}

esp_err_t websocket_server_start(const ws_inbound_message_handler_t ws_inbound_message_handler_fun) {
    ESP_LOGI(TAG, "Starting WebSocket server");
    if (server) {
        ESP_LOGE(TAG, "WebSocket server is already running");
        return ESP_FAIL;
    }

    // Register a function for the JSON request handler
    inbound_message_handler_fun = ws_inbound_message_handler_fun;

    // Initialize the WebSocket client management system
    ESP_LOGI(TAG, "Initializing WebSocket client management system");

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
        .handler = inbound_message_handler,
        .is_websocket = true
    };
    ret = httpd_register_uri_handler(server, &ws_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WebSocket URI: %s", esp_err_to_name(ret));
        httpd_stop(server);
        server = nullptr;
        websocket_client_cleanup();
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
        websocket_client_cleanup();
        ESP_LOGI(TAG, "WebSocket server stopped");
    } else {
        ESP_LOGE(TAG, "Failed to stop WebSocket server: %s", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "WebSocket server stopped");

    return ret;
}

esp_err_t websocket_send_message_to_client(const int fd, const char *message) {
    if (!server || !message) return ESP_ERR_INVALID_ARG;

    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)message,
        .len = strlen(message)
    };

    return httpd_ws_send_frame_async(server, fd, &frame);
}

esp_err_t websocket_broadcast_message(const char *message) {
    if (!server || !message) return ESP_ERR_INVALID_ARG;

    websocket_client_t *c;
    esp_err_t last_err = ESP_OK;

    LIST_FOREACH(c, &clients, entries) {
        esp_err_t err = websocket_send_message_to_client(c->fd, message);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send to client %d: %s", c->fd, esp_err_to_name(err));
            websocket_client_remove(c->fd);  // Remove the failed client
            last_err = err;
        }
    }

    return last_err;
}
