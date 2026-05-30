#ifndef JSON_REQUEST_HANDLER_H
#define JSON_REQUEST_HANDLER_H

#include <esp_err.h>
#include "websocket_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handles an incoming JSON request: parses, authenticates, and dispatches the command.
 *
 * @param inbound_message The incoming JSON message as a string.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t handle_json_inbound_message(const char *inbound_message, int client_fd);
void handle_websocket_client_event(ws_client_event_t event, int client_fd);

#ifdef __cplusplus
}
#endif

#endif // JSON_REQUEST_HANDLER_H
