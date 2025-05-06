#ifndef MATTER_UTIL_H
#define MATTER_UTIL_H

#include <esp_err.h>
#include <esp_matter_controller_utils.h>
#include <esp_matter_core.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t matter_controller_init(uint64_t node_id, uint64_t fabric_id, uint16_t listen_port,
                                 void (*read_attribute_data_callback)(
                                     uint64_t,
                                     const chip::app::ConcreteDataAttributePath &,
                                     chip::TLV::TLVReader *),
                                 void (*subscribe_done_callback)(uint64_t remote_node_id, uint32_t subscription_id)
);

/**
 * @brief Commission a Matter device using BLE and Thread dataset.
 *
 * @param node_id        Unique node identifier to assign.
 * @param pin            Setup passcode (typically 8-digit).
 * @param discriminator  12-bit discriminator for device discovery.
 * @param dataset_tlvs   TLV-encoded Thread operational dataset.
 * @param dataset_len    Length of the dataset_tlvs buffer.
 * @return esp_err_t     ESP_OK on success, error code otherwise.
 */
esp_err_t pairing_ble_thread(uint64_t node_id, uint32_t pin, uint16_t discriminator, uint8_t *dataset_tlvs,
                             size_t dataset_len);

/**
 * @brief Invoke a command on a Matter cluster.
 *
 * @param destination_id       Target node ID.
 * @param endpoint_id          Endpoint on the target node.
 * @param cluster_id           Cluster ID containing the command.
 * @param command_id           ID of the command to invoke.
 * @param command_data_field   Command data payload as a JSON string.
 * @return esp_err_t           ESP_OK on success, error code otherwise.
 */
esp_err_t invoke_cluster_command(uint64_t destination_id, uint16_t endpoint_id, uint32_t cluster_id,
                                 uint32_t command_id, const char *command_data_field);

/**
 * @brief Send a read request for a specific attribute.
 *
 * @param node_id                     Target node ID.
 * @param endpoint_id                 Endpoint containing the attribute.
 * @param cluster_id                  Cluster ID of the attribute.
 * @param attribute_id                Attribute ID to read.
 * @return esp_err_t                  ESP_OK on success, error code otherwise.
 */
esp_err_t send_read_attr_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id);

/**
 * @brief Subscribe to a specific attribute and receive updates.
 *
 * @param node_id           Target node ID.
 * @param endpoint_id       Endpoint containing the attribute.
 * @param cluster_id        Cluster ID containing the attribute.
 * @param attribute_id      ID of the attribute to subscribe to.
 * @param min_interval      Minimum reporting interval (in seconds).
 * @param max_interval      Maximum reporting interval (in seconds).
 * @param auto_resubscribe  Automatically resubscribe on connection loss.
 * @return esp_err_t        ESP_OK on success, error code otherwise.
 */
esp_err_t send_subscribe_attr_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, uint16_t min_interval, uint16_t max_interval,
                                      bool auto_resubscribe);

#ifdef __cplusplus
}
#endif

#endif // MATTER_UTIL_H
