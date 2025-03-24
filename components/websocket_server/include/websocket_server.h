#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <esp_err.h>
#include <esp_http_server.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

    // Start the WebSocket server
    esp_err_t websocket_server_start();

    // Stop the WebSocket server
    esp_err_t websocket_server_stop();

    // Send a message to a WebSocket client
    esp_err_t websocket_send_message(int client_fd, const char *message);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_SERVER_H