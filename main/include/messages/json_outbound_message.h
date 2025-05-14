#ifndef JSON_OUTBOUND_MESSAGE_H
#define JSON_OUTBOUND_MESSAGE_H

#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- THREAD ----

/**
 * Sends the Thread Stack status
 */
esp_err_t create_info_thread_stack_status_message(bool is_running);

/**
 * Sends the Network Interface status (up/down)
 */
esp_err_t create_info_thread_interface_status_message(bool is_up);

/**
 * Sends the attachment status (attached/detached)
 */
esp_err_t create_info_thread_attachment_status_message(bool is_attached);

/**
 * Sends the current Thread device role (e.g., leader, router, child)
 */
esp_err_t create_info_thread_role_message(const char *role);

/**
 * Sends the list of unicast IPv6 addresses
 */
esp_err_t create_info_unicast_addresses_message(const char **addresses, size_t count);

/**
 * Sends the list of multicast IPv6 addresses
 */
esp_err_t create_info_multicast_addresses_message(const char **addresses, size_t count);

/**
 * Sends the MeshCoP service status (published or removed)
 */
esp_err_t create_info_meshcop_service_status_message(bool is_published);

/**
 * Sends the current active dataset values
 */
esp_err_t create_info_active_dataset_message(
    uint64_t active_timestamp,
    const char *network_name,
    const uint8_t *extended_pan_id,
    const uint8_t *mesh_local_prefix,
    uint16_t pan_id,
    uint16_t channel);

// ---- WI-FI ----

esp_err_t create_info_wifi_status_message(const char *status);

// ---- MATTER ----

esp_err_t create_info_matter_commissioning_complete_message(uint64_t nodeId, uint8_t fabricIndex);

esp_err_t create_info_matter_attribute_report_message(uint64_t nodeId, uint16_t endpointId, uint32_t clusterId,
                                                      uint32_t attributeId, const char *value);

esp_err_t create_info_matter_subscribe_done_message(uint64_t nodeId, uint32_t subscription_id);

#ifdef __cplusplus
}
#endif

#endif // JSON_OUTBOUND_MESSAGE_H
