#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*ws_connection_handler_t)(int client_fd);

typedef esp_err_t (*ws_inbound_message_handler_t)(const char *json);

/**
 * Starts the WebSocket server and initializes its necessary components.
 *
 * This function sets up the WebSocket server to begin accepting connections,
 * registers a handler for processing inbound messages received on active WebSocket sessions,
 * and optionally sends a message to newly connected clients.
 *
 * @param connection_handler_fun A pointer to a function that will be called when a new WebSocket connection is established.
 * @param message_handler_fun A pointer to a function that will be called when a new message is received on an active WebSocket session.
 * @return
 * - ESP_OK on successful initialization and start of the WebSocket server.
 * - ESP_ERR_INVALID_ARG if the handler parameter is invalid.
 * - Other error codes indicating failures during initialization.
 */
esp_err_t websocket_server_start(ws_connection_handler_t connection_handler_fun,
                                 ws_inbound_message_handler_t message_handler_fun);

/**
 * Stops the WebSocket server and cleans up associated resources.
 *
 * This function halts the WebSocket server if it is currently running,
 * stops the keep-alive mechanism for the WebSocket connections,
 * and deallocates resources used by the server.
 *
 * @return
 * - ESP_OK on a successful server stop operation.
 * - ESP_FAIL if the server was not running.
 */
esp_err_t websocket_server_stop(void);

/**
 * Sends a WebSocket message to a specific client asynchronously.
 *
 * This function queues a message to be sent via WebSocket to a specific client,
 * identified by the given file descriptor (fd). The message is sent as a
 * non-fragmented, text-based WebSocket frame.
 * The sending operation is asynchronous and does not block the caller.
 *
 * The server instance must be properly initialized and running before
 * invoking this function.
 *
 * The function allocates memory for the message and sends it in a separate
 * asynchronous task to avoid blocking the main execution thread.
 *
 * @param fd The file descriptor of the WebSocket connection for the specific client.
 *           This represents the client's identifier within the HTTPD server.
 * @param message Pointer to a null-terminated C-string containing the message to be sent.
 *                The message is expected to be valid and non-null.
 *
 * @return
 *         - ESP_OK on successful queuing of the message for sending.
 *         - ESP_ERR_INVALID_ARG if the server instance has not been initialized
 *           or if the message parameter is invalid (e.g., null).
 *         - Other error codes might be returned based on the result of internal operations.
 */
esp_err_t websocket_send_message_to_client(int fd, const char *message);

/**
 * @brief Broadcasts a message to all connected WebSocket clients.
 *
 * This function sends the specified message to all clients currently connected to the WebSocket server.
 * It filters out connected clients that are in WebSocket mode and delivers the message as a WebSocket text frame.
 *
 * @param message The message to broadcast. Must be a null-terminated string. The message is sent as-is
 *                to all connected WebSocket clients.
 *
 * @return
 *     - ESP_OK: The message was successfully sent to all valid clients.
 *     - ESP_ERR_INVALID_ARG: The server or message parameter is invalid (e.g., the server is not initialized or a message is null).
 *     - ESP_FAIL: Failed to retrieve the client list or send the frame to one or more clients.
 */
esp_err_t websocket_broadcast_message(const char *message);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_SERVER_H
