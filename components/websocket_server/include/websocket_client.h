#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <sys/queue.h>
#include <freertos/FreeRTOS.h>

#ifdef __cplusplus
extern "C" {
#endif

    // Represents a connected WebSocket client
    typedef struct websocket_client {
        int fd;                     // Client file descriptor
        bool authenticated;         // Authentication status
        TickType_t connect_time;    // Timestamp when the client connected
        LIST_ENTRY(websocket_client) entries; // Linked list entry
    } websocket_client_t;

    // Initialize the client management system
    void websocket_client_init();

    // Add a new client to the management system
    void websocket_client_add(int fd);

    // Remove a client from the management system
    void websocket_client_remove(int fd);

    // Find a client by file descriptor
    websocket_client_t* websocket_client_find(int fd);

    // Check if a client is authenticated
    bool websocket_client_is_authenticated(int fd);

    // Mark a client as authenticated
    void websocket_client_authenticate(int fd);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_CLIENT_H