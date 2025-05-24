#ifndef COMMANDS_H
#define COMMANDS_H

#include <esp_event.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes the Matter controller to handle commands within a Matter-enabled network.
 *
 * @param config The configuration parameters required to initialize the controller.
 * @return A result code indicating the success or failure of the initialization process.
 */
esp_err_t execute_matter_controller_init_command(uint64_t node_id, uint64_t fabric_id, uint16_t listen_port);

/**
 * Executes the Matter BLE (Bluetooth Low Energy) pairing process for a Thread network
 * using the specified node ID, PIN, and discriminator values.
 *
 * This method retrieves the active Thread dataset TLVs (Type Length Values),
 * and uses them in the BLE pairing process.
 *
 * @param node_id The unique identifier for the node.
 * @param pin The PIN (Personal Identification Number) used for pairing.
 * @param discriminator The discriminator value used in identifying the Matter network.
 * @return
 *         - ESP_OK: If the pairing process was successful.
 *         - ESP_ERR_NO_MEM: If memory allocation failed.
 *         - Other esp_err_t values indicating specific error conditions during dataset
 *           retrieval or pairing.
 */
esp_err_t execute_matter_pair_ble_thread_command(uint64_t node_id, uint32_t pin, uint16_t discriminator);

/**
 * Executes a command to invoke a Matter cluster-specific command.
 *
 * @param destination_id The unique identifier of the destination device.
 * @param endpoint_id The endpoint identifier within the destination device to address.
 * @param cluster_id The identifier of the cluster associated with the command.
 * @param command_id The identifier of the command to be invoked within the cluster.
 * @param payload_json A JSON-formatted string containing the payload for the command.
 * @return A result code of type esp_err_t. ESP_OK on success, or an appropriate error code on failure.
 */
esp_err_t execute_cmd_invoke_command(uint64_t destination_id, uint16_t endpoint_id, uint32_t cluster_id,
                                     uint32_t command_id, const char *payload_json);

/**
 * Executes an attribute read command for a specific node, endpoint, cluster, and attribute.
 *
 * @param node_id The unique identifier of the target node.
 * @param endpoint_id The endpoint on the node where the attribute resides.
 * @param cluster_id The identifier of the cluster to which the attribute belongs.
 * @param attribute_id The identifier of the attribute to be read.
 * @return An esp_err_t indicating success or the type of error encountered during execution.
 */
esp_err_t execute_attr_read_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id,
                                    uint32_t attribute_id);

/**
 * Executes an attribute subscription command for a specified node, endpoint, cluster, and attribute,
 * with defined minimum and maximum reporting intervals.
 *
 * @param node_id ID of the target node for the subscription command.
 * @param endpoint_id ID of the endpoint where the attribute is located.
 * @param cluster_id ID of the cluster to which the attribute belongs.
 * @param attribute_id ID of the attribute to be subscribed.
 * @param min_interval Minimum reporting interval, in seconds.
 * @param max_interval Maximum reporting interval, in seconds.
 * @return `ESP_OK` on success, or an appropriate error code if the command fails.
 */
esp_err_t execute_attr_subscribe_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, uint16_t min_interval, uint16_t max_interval);


#ifdef __cplusplus
}
#endif

#endif //COMMANDS_H
