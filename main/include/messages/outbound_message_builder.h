#ifndef JSON_OUTBOUND_MESSAGE_H
#define JSON_OUTBOUND_MESSAGE_H

#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- WEBSOCKET ----

/**
 * Sends a WebSocket "connected" message to the specified client.
 *
 * This function constructs a JSON message with the type "websocket" and
 * the action "connected", and includes a payload indicating the connection
 * status. The message is then sent to the client identified by the provided
 * file descriptor.
 *
 * @param client_fd The file descriptor of the connected client to which the
 *                  message will be sent.
 * @return ESP_OK if the message is successfully sent, or an appropriate error
 *         code if the operation fails.
 */
esp_err_t send_websocket_connected_message_to_client(int client_fd);

// ---- THREAD ----

/**
 * Broadcasts an informational message about the thread stack's status.
 *
 * This function builds a JSON payload containing the current status of the thread stack,
 * indicating whether it is running or not, and then broadcasts the message using a
 * WebSocket connection.
 *
 * @param is_running A boolean indicating the current status of the thread stack.
 *                   `true` if the stack is running, `false` otherwise.
 * @return ESP_OK if the message was successfully broadcast, or an error code otherwise.
 */
esp_err_t broadcast_info_thread_stack_status_message(bool is_running);

/**
 * Broadcasts a status message indicating whether the Thread interface is up or down.
 *
 * @param is_up A boolean indicating the status of the Thread interface.
 *              Pass `true` if the interface is up, or `false` if it is down.
 * @return An `esp_err_t` indicating the success or failure of the operation.
 *         Returns ESP_OK on success or an appropriate error code on failure.
 */
esp_err_t broadcast_info_thread_interface_status_message(bool is_up);

/**
 * Broadcasts a message indicating the thread attachment status.
 * This method creates a JSON payload with the attachment status
 * and sends it via a WebSocket broadcast.
 *
 * @param is_attached Indicates whether the thread is currently attached
 *                    (true if attached, false if detached).
 * @return ESP_OK if the message was successfully broadcast, or an
 *         error code of type esp_err_t in case of failure.
 */
esp_err_t broadcast_info_thread_attachment_status_message(bool is_attached);

/**
 * Broadcasts a JSON message containing the given Thread role information to all connected WebSocket clients.
 *
 * The message contains the key "role" with a string value that represents the Thread role.
 *
 * @param role A pointer to a null-terminated string representing the Thread role. Must not be null.
 * @return
 *         - ESP_OK on successful broadcast of the message.
 *         - ESP_ERR_INVALID_ARG if the role parameter is null.
 *         - ESP_FAIL if there is an error while creating the JSON object or broadcasting the message.
 */
esp_err_t broadcast_info_thread_role_message(const char *role);

/**
 * Broadcasts a message containing a list of unicast addresses.
 *
 * This function creates a JSON payload with the provided array of unicast addresses
 * and broadcasts it using the WebSocket server.
 *
 * @param addresses An array of C strings representing unicast addresses.
 *                  Each string must be null-terminated. The array must not be null.
 * @param count The number of addresses in the `addresses` array.
 *              If `count` is zero, an empty list will be sent.
 * @return ESP_OK on successful broadcasting of the message.
 *         ESP_FAIL if an error occurs during the creation or broadcasting of the message.
 */
esp_err_t broadcast_info_unicast_addresses_message(const char **addresses, size_t count);

/**
 * Broadcasts a JSON-formatted message containing a list of multicast addresses.
 *
 * The function takes an array of multicast addresses and constructs a JSON message
 * with the addresses formatted as a list. This message is broadcast using the
 * specified broadcasting mechanism.
 *
 * @param addresses An array of strings, where each string is a multicast address.
 *                  The array should not be null, and the individual addresses
 *                  should be null-terminated strings. Null entries in the array
 *                  are ignored.
 * @param count The number of addresses in the array.
 * @return ESP_OK on successful broadcast of the message. Returns ESP_FAIL if an
 *         error occurs while constructing or broadcasting the message.
 */
esp_err_t broadcast_info_multicast_addresses_message(const char **addresses, size_t count);

/**
 * Broadcasts an information message about the MeshCop service status.
 *
 * This function builds a JSON payload containing the "published" status
 * and broadcasts it using the "info" type and "thread.meshcop_service" action.
 *
 * @param is_published A boolean indicating whether the MeshCop service is published
 *                     (true) or removed (false).
 * @return ESP_OK if the message broadcasting is successful, an appropriate
 *         ESP-IDF error code otherwise (e.g., ESP_FAIL).
 */
esp_err_t broadcast_info_meshcop_service_status_message(bool is_published);

/**
 * Broadcasts a "thread.active_dataset" message with the provided active dataset details.
 *
 * This function formats the dataset information into a JSON payload and sends it
 * as a broadcast message using a WebSocket communication mechanism.
 *
 * @param active_timestamp The active timestamp of the dataset.
 * @param network_name The name of the network in the dataset.
 * @param extended_pan_id The extended PAN ID of the network, represented as an 8-byte array.
 * @param mesh_local_prefix The mesh-local prefix of the network, represented as an 8-byte array.
 * @param pan_id The PAN ID of the network.
 * @param channel The channel on which the network operates.
 * @return ESP_OK if the message was broadcast successfully, ESP_ERR_INVALID_ARG for invalid arguments,
 *         or ESP_FAIL for any other failure.
 */
esp_err_t broadcast_info_active_dataset_message(
    uint64_t active_timestamp,
    const char *network_name,
    const uint8_t *extended_pan_id,
    const uint8_t *mesh_local_prefix,
    uint16_t pan_id,
    uint16_t channel);

// ---- WI-FI ----

/**
 * Broadcasts an informational Wi-Fi status message to all connected WebSocket clients.
 *
 * This function creates a JSON-encoded message containing the provided Wi-Fi status
 * and broadcasts it using WebSocket communication. The message type is set as "info"
 * with the action "wifi.sta_status". The provided status is included in the message payload.
 *
 * @param status A constant character pointer representing the Wi-Fi status (e.g., "connected", "disconnect").
 *               Must not be null.
 * @return Returns `ESP_OK` on successful broadcasting of the message.
 *         Returns `ESP_FAIL` if an error occurs during message creation or broadcasting.
 */
esp_err_t broadcast_info_wifi_status_message(const char *status);

// ---- MATTER ----

/**
 * Broadcasts a message indicating that Matter device commissioning is complete.
 *
 * This function constructs a JSON message that includes the node ID and fabric index
 * of the commissioned Matter device and broadcasts it to all connected WebSocket clients.
 *
 * @param nodeId The 64-bit Node ID of the commissioned Matter device.
 * @param fabricIndex The 8-bit Fabric Index associated with the device.
 * @return ESP_OK on successful broadcast; ESP_FAIL or ESP_ERR_INVALID_ARG on failure.
 */
esp_err_t broadcast_info_matter_commissioning_complete_message(uint64_t nodeId, uint8_t fabricIndex);

/**
 * Broadcasts a message containing a Matter attribute report.
 *
 * This function builds and sends a JSON message including the node ID, endpoint ID,
 * cluster ID, attribute ID, and reported value. It is typically used when receiving
 * attribute report data from a Matter device.
 *
 * @param nodeId The 64-bit Node ID of the device.
 * @param endpointId The 16-bit endpoint ID.
 * @param clusterId The 32-bit cluster ID.
 * @param attributeId The 32-bit attribute ID being reported.
 * @param value The attribute value as a string. Must not be null.
 * @return ESP_OK on success, or ESP_ERR_INVALID_ARG / ESP_FAIL on failure.
*/
esp_err_t broadcast_info_matter_attribute_report_message(uint64_t nodeId, uint16_t endpointId, uint32_t clusterId,
                                                         uint32_t attributeId, const char *value);

/**
 * Broadcasts an information message indicating that a Matter subscription has successfully completed.
 *
 * @param nodeId The unique identifier of the node for which the subscription has completed.
 * @param subscription_id The identifier of the completed subscription.
 * @return `ESP_OK` if the message was successfully broadcast, otherwise an error code.
 */
esp_err_t broadcast_info_matter_subscribe_done_message(uint64_t nodeId, uint32_t subscription_id);

#ifdef __cplusplus
}
#endif

#endif // JSON_OUTBOUND_MESSAGE_H
