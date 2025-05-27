#include "websocket_server.h"
#include <esp_log.h>
#include <esp_https_server.h>
#include <esp_timer.h>
#include <unistd.h>
#include "keep_alive.h"

// Max number of clients that can connect to the WebSocket server simultaneously.
constexpr int MAX_CLIENTS = 10;

// URI path for the WebSocket server endpoint.
constexpr char WEBSOCKET_URI[] = "/ws";

// Static instance of the HTTP server.
static httpd_handle_t server = nullptr;

// Static variable to store the WebSocket inbound message handler callback.
static ws_inbound_message_handler_t message_handler = nullptr;

// Static variable to manage and monitor websocket client connections for the server.
static wss_keep_alive_t keep_alive = nullptr;

// The start address of the server certificate in PEM format.
extern const char servercert_pem_start[] asm("_binary_servercert_pem_start");
// The end marker for the server certificate's PEM file contents embedded in the binary.
extern const char servercert_pem_end[] asm("_binary_servercert_pem_end");
// Externally declared constant array representing the start of a private key in PEM format.
extern const char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
// The end of the private key PEM file in binary form.
extern const char prvtkey_pem_end[] asm("_binary_prvtkey_pem_end");

// Structure for holding arguments required to send asynchronous data over a WebSocket.
struct AsyncSendArg {
    // Handle to the HTTP server instance.
    httpd_handle_t httpd_handle;
    // File descriptor for a client connection.
    int client_fd;
    // A pointer to a character array (C-style string) that holds the message to be sent to a websocket client.
    char *message;
};

/**
 * Frees the memory associated with an AsyncSendArg structure,
 * including its message member.
 *
 * @param arg A pointer to the AsyncSendArg structure to be freed.
 *            This includes the message member and the structure itself.
 */
static void free_async_send_arg(AsyncSendArg *arg) {
    free(arg->message);
    free(arg);
}

/**
 * @brief Asynchronously sends a WebSocket message to a client.
 *
 * This function creates a WebSocket frame and sends it asynchronously to
 * the specified client using the provided HTTPD handle. After sending
 * the frame, the allocated resources for the argument are freed.
 *
 * @param arg Pointer to the AsyncSendArg structure containing the HTTPD handle,
 * the client file descriptor, and the message to be sent.
 */
static void async_send_task(void *arg) {
    auto *send_arg = static_cast<AsyncSendArg *>(arg);
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = reinterpret_cast<uint8_t *>(send_arg->message),
        .len = strlen(send_arg->message)
    };
    httpd_ws_send_frame_async(send_arg->httpd_handle, send_arg->client_fd, &frame);
    free_async_send_arg(send_arg);
}

/**
 * Handles the cleanup process when a client connection is closed.
 *
 * This function removes the client from the keep-alive mechanism and closes
 * the associated file descriptor to ensure proper resource management.
 *
 * @param handle  The HTTP server handle (unused in this function).
 * @param fd      The file descriptor of the client connection to be closed.
 */
static void on_client_close(httpd_handle_t handle, const int fd) {
    wss_keep_alive_remove_client(keep_alive, fd);
    close(fd);
}

/**
 * @brief Creates and initializes an SSL configuration for the HTTP server.
 *
 * This function sets up an httpd_ssl_config_t structure with the default SSL configuration
 * and customizes it to include server certificate, private key, and user context. It also
 * assigns the custom client close callback function.
 *
 * @return A fully initialized httpd_ssl_config_t object containing SSL configuration settings.
 */
static httpd_ssl_config_t create_ssl_config() {
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd.global_user_ctx = keep_alive;
    config.httpd.close_fn = &on_client_close;
    config.servercert = reinterpret_cast<const uint8_t *>(servercert_pem_start);
    config.servercert_len = servercert_pem_end - servercert_pem_start;
    config.prvtkey_pem = reinterpret_cast<const uint8_t *>(prvtkey_pem_start);
    config.prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;
    return config;
}

/**
 * Sends a WebSocket ping frame to a specified client.
 *
 * This function sends a WebSocket ping frame asynchronously to a given client identified by
 * the file descriptor. It uses the user context obtained from the keep-alive handler
 * to perform this operation. If the ping fails to send, an error is logged.
 *
 * @param h The handle to the WebSocket keep-alive context.
 * @param fd The file descriptor of the WebSocket client to which the ping should be sent.
 * @return true if the ping frame was successfully sent, false otherwise.
 */
static bool send_ping_to_client(wss_keep_alive_t h, const int fd) {
    auto *hd = wss_keep_alive_get_user_ctx(h);
    httpd_ws_frame_t ping = {.type = HTTPD_WS_TYPE_PING, .payload = nullptr, .len = 0};
    esp_err_t err = httpd_ws_send_frame_async(hd, fd, &ping);
    if (err != ESP_OK) {
        ESP_LOGE("websocket_server", "Failed to send ping to fd=%d: %s", fd, esp_err_to_name(err));
        return false;
    }
    ESP_LOGD("websocket_server", "Ping sent to fd=%d", fd);
    return true;
}

/**
 * Processes an incoming WebSocket frame and performs actions based
 * on the frame type. Handles text frames, pong frames, and close frames.
 *
 * @param frame The WebSocket frame to be processed.
 * @param fd The file descriptor of the client connection associated
 *           with the WebSocket frame.
 */
static void process_frame(const httpd_ws_frame_t &frame, const int fd) {
    switch (frame.type) {
        case HTTPD_WS_TYPE_TEXT:
            if (message_handler) {
                message_handler(reinterpret_cast<const char *>(frame.payload));
            }
            break;
        case HTTPD_WS_TYPE_PONG:
            ESP_LOGD("websocket_server", "Received pong from fd=%d", fd);
            wss_keep_alive_client_is_active(keep_alive, fd);
            break;
        case HTTPD_WS_TYPE_CLOSE:
            wss_keep_alive_remove_client(keep_alive, fd);
            break;
        default:
            ESP_LOGW("websocket_server", "Unhandled frame type: %d", frame.type);
            break;
    }
}

/**
 * Receives and processes a WebSocket frame from an HTTP request.
 *
 * This method handles the reception of a WebSocket frame from the client,
 * processes its payload if present, and calls the appropriate frame type handler.
 * It also ensures proper memory management for the frame payload and logs errors
 * in case of failures.
 *
 * @param req Pointer to the HTTP request object that contains the WebSocket frame.
 * @return
 *     - ESP_OK: The WebSocket frame was received and handled successfully.
 *     - ESP_ERR_NO_MEM: Memory allocation for the payload failed.
 *     - Other esp_err_t codes: Errors from the underlying WebSocket frame
 *       reception function.
 */
static esp_err_t receive_and_handle_frame(httpd_req_t *req) {
    httpd_ws_frame_t frame = {};
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGE("websocket_server", "Failed to receive frame header: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD("websocket_server", "Received frame: type=%d, len=%d", frame.type, frame.len);

    if (frame.len > 0) {
        auto *buf = static_cast<uint8_t *>(calloc(1, frame.len + 1));
        if (!buf) {
            ESP_LOGE("websocket_server", "Memory allocation failed for payload");
            return ESP_ERR_NO_MEM;
        }
        frame.payload = buf;
        ret = httpd_ws_recv_frame(req, &frame, frame.len);
        if (ret == ESP_OK) {
            process_frame(frame, httpd_req_to_sockfd(req));
        } else {
            ESP_LOGE("websocket_server", "Failed to receive payload: %s", esp_err_to_name(ret));
        }
        free(buf);
    } else {
        frame.payload = nullptr;
        process_frame(frame, httpd_req_to_sockfd(req));
    }

    return ret;
}

/**
 * Handles incoming websocket requests and processes connection or message frames.
 *
 * @param req Pointer to the HTTP request associated with the websocket event.
 *            This contains information about the request method and metadata for
 *            the active connection.
 * @return
 *         - ESP_OK if the request is successfully processed (e.g., WebSocket connection established).
 *         - Corresponding error code returned by `receive_and_handle_frame` for non-GET methods.
 */
static esp_err_t websocket_handler(httpd_req_t *req) {
    const int fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        ESP_LOGI("websocket_server", "Client connected: fd=%d", fd);
        wss_keep_alive_add_client(keep_alive, fd);
        if (connection_handler) {
            connection_handler(fd);
        }
        return ESP_OK;
    }

    return receive_and_handle_frame(req);
}
esp_err_t websocket_server_start(const ws_inbound_message_handler_t message_handler_fun) {
    // Prevent starting if the server is already running
    if (server) return ESP_FAIL;

    // Set user-provided handler for message processing
    message_handler = message_handler_fun;

    // Configure keep-alive: handle inactive clients and ping checking
    wss_keep_alive_config_t ka_cfg = KEEP_ALIVE_CONFIG_DEFAULT();
    ka_cfg.client_not_alive_cb = [](wss_keep_alive_t h, const int fd) {
        // Close the session if a client fails to keep-alive
        httpd_sess_trigger_close(wss_keep_alive_get_user_ctx(h), fd);
        return true;
    };
    ka_cfg.check_client_alive_cb = [](const wss_keep_alive_t h, const int fd) {
        // Send ping to verify if a client is alive
        return send_ping_to_client(h, fd);
    };

    // Start the keep-alive monitor
    keep_alive = wss_keep_alive_start(&ka_cfg);

    // Configure and start the HTTPS WebSocket server
    httpd_ssl_config_t ssl_cfg = create_ssl_config();
    esp_err_t ret = httpd_ssl_start(&server, &ssl_cfg);
    if (ret != ESP_OK) return ret;

    // Define WebSocket URI handler
    static constexpr httpd_uri_t ws_uri = {
        .uri = WEBSOCKET_URI,
        .method = HTTP_GET,
        .handler = websocket_handler,
        .is_websocket = true,
        .handle_ws_control_frames = true
    };

    // Register the URI handler and link it to the keep-alive context
    httpd_register_uri_handler(server, &ws_uri);
    wss_keep_alive_set_user_ctx(keep_alive, server);

    return ESP_OK;
}

esp_err_t websocket_server_stop() {
    // Prevent stopping if the server is not running
    if (!server) return ESP_FAIL;

    // Stop the keep-alive system
    wss_keep_alive_stop(keep_alive);
    keep_alive = nullptr;

    // Stop HTTPS server
    server = nullptr;
    const esp_err_t ret = httpd_ssl_stop(server);

    return ret;
}

esp_err_t websocket_send_message_to_client(const int fd, const char *message) {
    // Validate server state and message input
    if (!server || !message) return ESP_ERR_INVALID_ARG;

    // Allocate and prepare an async send task
    auto *arg = static_cast<AsyncSendArg *>(malloc(sizeof(AsyncSendArg)));
    if (!arg) return ESP_ERR_NO_MEM;

    arg->httpd_handle = server;
    arg->client_fd = fd;
    arg->message = strdup(message);
    if (!arg->message) {
        free(arg);
        return ESP_ERR_NO_MEM;
    }

    // Queue task for asynchronous WebSocket transmission
    return httpd_queue_work(server, async_send_task, arg);
}

esp_err_t websocket_broadcast_message(const char *message) {
    // Validate input
    if (!server || !message) return ESP_ERR_INVALID_ARG;

    // Retrieve a list of connected WebSocket clients
    int fds[MAX_CLIENTS];
    size_t count = MAX_CLIENTS;
    if (httpd_get_client_list(server, &count, fds) != ESP_OK) return ESP_FAIL;

    // Iterate and send a message to all active WebSocket clients
    for (size_t i = 0; i < count; ++i) {
        if (httpd_ws_get_fd_info(server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            websocket_send_message_to_client(fds[i], message);
        }
    }

    return ESP_OK;
}
