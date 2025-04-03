#ifndef JSON_REQUEST_PARSER_H
#define JSON_REQUEST_PARSER_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parses a WebSocket request message and generates a response.
 *
 * @param request_message The incoming JSON message as a string.
 * @param isAuthenticated Indicates if the client is authenticated.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t handle_json_request(char *request_message, bool isAuthenticated);

#ifdef __cplusplus
}
#endif

#endif // JSON_REQUEST_PARSER_H
