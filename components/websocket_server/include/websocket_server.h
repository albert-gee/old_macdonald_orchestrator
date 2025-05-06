#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <esp_err.h>
#include <esp_http_server.h>
#include <sys/queue.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct websocket_client {
        int fd;
        LIST_ENTRY(websocket_client) entries;
    } websocket_client_t;

    typedef esp_err_t (*ws_inbound_message_handler_t)(const char *request_message);

    // Start the WebSocket server
    esp_err_t websocket_server_start(ws_inbound_message_handler_t ws_inbound_message_handler_fun);

    // Stop the WebSocket server
    esp_err_t websocket_server_stop();

    // Send a message to a client
    esp_err_t websocket_send_message_to_client(int fd, const char *message);

    // Send a message to all connected clients
    esp_err_t websocket_broadcast_message(const char *message);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_SERVER_H