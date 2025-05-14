#ifndef THREAD_UTIL_H
#define THREAD_UTIL_H

#include <esp_err.h>
#include <openthread/dataset.h>
#include <openthread/instance.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THREAD_ADDRESS_LIST_MAX 16

// -----------------------------------------------------------------------------
// Interface Control
// -----------------------------------------------------------------------------

/**
 * @brief Brings up the OpenThread IPv6 interface.
 *
 * Enables the IPv6 interface to allow communication over Thread.
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t ifconfig_up(void);

/**
 * @brief Brings down the OpenThread IPv6 interface.
 *
 * Disables IPv6 communication over Thread.
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t ifconfig_down(void);

/**
 * @brief Gets the OpenThread interface name.
 *
 * @param[out] interface_name Buffer to receive the interface name.
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t thread_get_interface_name(char *interface_name);

// -----------------------------------------------------------------------------
// Thread Stack Management
// -----------------------------------------------------------------------------

/**
 * @brief Starts the OpenThread stack.
 *
 * Allows the device to join or form a Thread network.
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t thread_start(void);

/**
 * @brief Stops the OpenThread stack.
 *
 * Disconnects the device from the Thread network.
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t thread_stop(void);

/**
 * @brief Initializes the active OpenThread dataset.
 *
 * Configures the dataset and applies it to the current instance.
 *
 * @param channel           Radio channel.
 * @param pan_id            Personal Area Network ID.
 * @param network_name      Thread network name.
 * @param extended_pan_id   Extended PAN ID as hex string (16 chars).
 * @param mesh_local_prefix Mesh-Local Prefix as IPv6 string (e.g., "fd00::").
 * @param network_key       Network master key as hex string (32 chars).
 * @param pskc              Pre-Shared Commissioner Key as hex string (32 chars).
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t thread_dataset_init(uint16_t channel,
                              uint16_t pan_id,
                              const char *network_name,
                              const char *extended_pan_id,
                              const char *mesh_local_prefix,
                              const char *network_key,
                              const char *pskc);

// -----------------------------------------------------------------------------
// Information Query APIs
// -----------------------------------------------------------------------------

/**
* @brief Checks whether the Thread stack is running (role != disabled).
*
* @param[out] is_running True if running, false otherwise.
* @return ESP_OK on success, otherwise ESP_FAIL.
*/
esp_err_t thread_is_stack_running(bool *is_running);

/**
 * @brief Checks whether the Thread node is attached (role != disabled/detached).
 *
 * @param[out] is_attached True if attached, false otherwise.
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t thread_is_attached(bool *is_attached);

/**
 * @brief Retrieves the current Thread device role as a string.
 *
 * @param[out] role_str Pointer to receive string constant (e.g., "leader", "child").
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t thread_get_device_role_string(const char **role_str);

/**
 * @brief Gets the list of current unicast IPv6 addresses.
 *
 * @param[out] out_addresses Array to store allocated address strings.
 * @param[in]  max           Maximum number of entries to retrieve.
 * @param[out] out_count     Actual number of addresses returned.
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t thread_get_unicast_addresses(char **out_addresses, size_t max, size_t *out_count);

/**
 * @brief Gets the list of current multicast IPv6 addresses.
 *
 * @param[out] out_addresses Array to store allocated address strings.
 * @param[in]  max           Maximum number of entries to retrieve.
 * @param[out] out_count     Actual number of addresses returned.
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t thread_get_multicast_addresses(char **out_addresses, size_t max, size_t *out_count);

/**
 * @brief Frees memory allocated by address list queries.
 *
 * @param[in] addresses Address string array returned by `thread_get_*_addresses`.
 * @param[in] count     Number of elements in the array.
 */
void thread_free_address_list(char **addresses, size_t count);

/**
 * @brief Gets the active operational dataset.
 *
 * @param[out] dataset Pointer to an `otOperationalDataset` struct.
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t thread_get_active_dataset(otOperationalDataset *dataset);

/**
 * @brief Gets the active operational dataset as TLVs.
 *
 * @param[out] dataset_tlvs Buffer to receive TLVs.
 * @param[inout] dataset_len Buffer length input and output actual length.
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t thread_get_active_dataset_tlvs(uint8_t *dataset_tlvs, uint8_t *dataset_len);

// -----------------------------------------------------------------------------
// Border Router
// -----------------------------------------------------------------------------

/**
 * @brief Initializes the OpenThread Border Router configuration.
 *
 * Registers the backbone interface and sets up the BR stack.
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t thread_br_init();

/**
 * @brief Deinitializes the OpenThread Border Router configuration.
 *
 * Unregisters the backbone interface and stops the BR stack.
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t thread_br_deinit();

#ifdef __cplusplus
}
#endif

#endif // THREAD_UTIL_H
