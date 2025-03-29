#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <sys/queue.h>
#include <freertos/FreeRTOS.h>
#include <stdbool.h>
#include <freertos/semphr.h>

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

    extern struct client_list clients;

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
    esp_err_t websocket_client_authenticate(int fd);

    // Client iteration callback function type
    typedef esp_err_t (*websocket_client_callback_t)(websocket_client_t* client, void* arg);

    // Iterate through all clients and apply callback
    // Returns ESP_OK if all callbacks succeeded, or first error encountered
    esp_err_t websocket_client_for_each(websocket_client_callback_t callback, void* arg);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_CLIENT_H
