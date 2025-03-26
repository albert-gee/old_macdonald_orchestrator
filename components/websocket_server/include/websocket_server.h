#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <esp_err.h>
#include <esp_http_server.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef esp_err_t (*json_request_handler_t)(char *request_message, char **response_message, bool isAuthenticated);

    // Start the WebSocket server
    esp_err_t websocket_server_start(json_request_handler_t json_request_handler);

    // Stop the WebSocket server
    esp_err_t websocket_server_stop();

    // Send a message to all WebSocket clients
    esp_err_t websocket_send_message_to_all_clients(const char *message);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_SERVER_H