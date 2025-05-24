#ifndef THREAD_COMMANDS_H
#define THREAD_COMMANDS_H

#include <esp_event.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Stack Control ----

/**
 * @brief Enables Thread protocol functionalities by bringing the interface up and starting the Thread stack.
 *
 * This function performs two primary steps for enabling Thread functionalities:
 * 1. It invokes `ifconfig_up()` to bring the network interface up.
 * 2. It invokes `thread_start()` to initialize and start the Thread stack.
 *
 * If any of these steps fails, an appropriate error message is logged with the tag "THREAD_COMMANDS" and the specific
 * ESP-IDF error code is returned.
 *
 * @return ESP_OK on successful execution of all steps.
 * @return Appropriate error code (e.g., ESP_FAIL) if any step fails.
 */
esp_err_t execute_thread_enable_command(void);

/**
 * @brief Disables the Thread stack by stopping its operation and
 *        bringing down the associated network interface.
 *
 * This function executes a sequence of operations required to
 * properly disable the Thread stack. It first stops the Thread
 * stack operation and then ensures that the associated
 * network interface is brought down.
 *
 * @return
 *     - ESP_OK: The Thread stack and network interface have been
 *               successfully disabled.
 *     - Appropriate ESP_ERR code: If an error occurred during the
 *                                 disable sequence.
 */
esp_err_t execute_thread_disable_command(void);

// ---- Dataset ----

/**
 * @brief Executes the initialization of a Thread network dataset with the given parameters.
 *
 * This function validates the provided inputs and initializes the dataset parameters
 * for configuring a Thread network. It leverages the `thread_dataset_init` function to
 * apply the configuration.
 *
 * @param[in] channel             The Thread channel to use (11-26).
 * @param[in] pan_id              The PAN ID for the Thread network.
 * @param[in] network_name        The name of the Thread network.
 * @param[in] extended_pan_id     The extended PAN ID for the Thread network.
 * @param[in] mesh_local_prefix   The Mesh Local Prefix (IPv6 prefix) to use.
 * @param[in] network_key         The network key for the Thread network.
 * @param[in] pskc                The Pre-Shared Key for Commissioner (PSKc) for securing Thread operations.
 *
 * @return
 *     - ESP_OK on success.
 *     - ESP_ERR_INVALID_ARG if any input parameter is invalid.
 *     - Other error codes from `thread_dataset_init` function.
 */
esp_err_t execute_thread_dataset_init_command(uint16_t channel, uint16_t pan_id, const char *network_name,
                                              const char *extended_pan_id, const char *mesh_local_prefix,
                                              const char *network_key, const char *pskc);

// ---- Status / Monitoring ----

/**
 * @brief Executes the 'thread.status_get' command to retrieve the current running status of the Thread stack.
 *
 * This function invokes the `thread_is_stack_running` function to determine whether the Thread stack
 * is currently running or not. The result is stored in the provided boolean pointer.
 *
 * @param[out] is_running Pointer to a boolean where the running status of the Thread stack will be stored.
 *                        - `true`: The Thread stack is running.
 *                        - `false`: The Thread stack is not running.
 *
 * @return
 * - `ESP_OK`: The command executed successfully, and the running status was retrieved.
 * - An appropriate error code from the Espressif error codes if the command fails.
 */
esp_err_t execute_thread_status_get_command(bool *is_running);

/**
 * @brief Executes the command to fetch the current thread attachment state.
 *
 * This function checks whether the thread is currently attached to the Thread network.
 * The result of the operation is stored in the `is_attached` parameter, where `true` indicates
 * that the thread is attached and `false` indicates it is not attached.
 *
 * @param[out] is_attached Pointer to a boolean where the attachment state will be stored.
 *                         Must not be null.
 * @return
 *         - ESP_OK: The operation was successful, and the attachment state was retrieved.
 *         - Appropriate error code from the underlying `thread_is_attached` function in case of failure.
 */
esp_err_t execute_thread_attached_get_command(bool *is_attached);

/**
 * Executes the command to retrieve the role of a Thread device and returns the
 * role as a string through the provided pointer.
 *
 * This function utilizes the `thread_get_device_role_string` function to obtain
 * the role of a Thread device. The role information is passed back via the
 * `role_str` pointer.
 *
 * @param[out] role_str Pointer to a string that will be populated with the
 *                       device role upon successful execution.
 *
 * @return
 *    - ESP_OK: The role was successfully retrieved.
 *    - Appropriate error code indicating the nature of the failure.
 */
esp_err_t execute_thread_role_get_command(const char **role_str);

/**
 * @brief Fetches the active Thread network dataset and serializes it into a JSON string.
 *
 * This function retrieves the active Thread dataset, including details such as the network name,
 * channel, PAN ID, extended PAN ID, and mesh local prefix. The data is formatted into a JSON
 * string and stored in the provided buffer. If the dataset cannot be retrieved or memory
 * allocation for the JSON object fails, the function returns an error.
 *
 * @param[out] json_buf Pointer to a buffer where the JSON string representation of the dataset
 *                      will be stored.
 * @param[in] buf_size  Size of the provided buffer in bytes. The JSON string will be truncated
 *                      if it exceeds this size.
 *
 * @return
 *         - ESP_OK on success.
 *         - ESP_FAIL if the dataset could not be retrieved or JSON serialization fails.
 *         - ESP_ERR_NO_MEM if memory allocation for the JSON object fails.
 */
esp_err_t execute_thread_active_dataset_get_command(char *json_buf, size_t buf_size);

/**
 * @brief Retrieves the list of unicast addresses for the Thread network.
 *
 * This function queries the Thread stack to fetch the current unicast addresses
 * associated with the device in the Thread network. It provides a way to access
 * this information with a specified maximum limit for the number of addresses.
 *
 * @param[out] addresses  An array of character pointers where the retrieved unicast
 *                        addresses will be stored.
 * @param[in] max         The maximum number of unicast addresses that can be stored
 *                        in the provided addresses array.
 * @param[out] count      A pointer to a value that will be set to the actual number
 *                        of unicast addresses retrieved.
 *
 * @return
 *         - ESP_OK: Successfully retrieved the unicast addresses.
 *         - An error code from the esp_err_t list if the operation failed.
 */
esp_err_t execute_thread_unicast_addresses_get_command(char **addresses, size_t max, size_t *count);

/**
 * @brief Executes the command to retrieve the list of Thread multicast addresses.
 *
 * This function serves as a wrapper for `thread_get_multicast_addresses`, enabling
 * higher-level functionality to fetch multicast addresses associated with the
 * Thread protocol. The user provides buffers to hold the retrieved addresses
 * and their count, which are populated by the underlying implementation.
 *
 * @param[in] addresses A pointer to an array of character pointers that will
 *                      be filled with the multicast address strings.
 * @param[in] max       The maximum number of addresses that can be retrieved
 *                      and stored in the provided `addresses` array.
 * @param[out] count    A pointer to a variable where the total number of addresses
 *                      retrieved will be stored.
 *
 * @return
 *      - ESP_OK on success.
 *      - An appropriate error code (e.g., ESP_ERR_INVALID_ARG) on failure.
 */
esp_err_t execute_thread_multicast_addresses_get_command(char **addresses, size_t max, size_t *count);

// ---- Border Router ----

/**
 * @brief Executes the Thread Border Router initialization command.
 *
 * This function serves as a wrapper for the `thread_br_init` function, providing an interface
 * to initialize the Thread Border Router functionality. The implementation links the respective
 * command defined in the Thread stack backend, enabling functionality related to Thread Border Router operations.
 *
 * @return
 *     - ESP_OK on success.
 *     - Appropriate error code otherwise, as defined by the Thread stack.
 */
esp_err_t execute_thread_br_init_command(void);

/**
 * @brief Executes the Thread Border Router (BR) deinitialization command.
 *
 * This function calls the `thread_br_deinit` function defined in the
 * `thread_util.h` file to deinitialize the Thread Border Router. It is invoked
 * when the action "thread.br_deinit" is processed. Returns the result of the
 * deinitialization operation.
 *
 * @return
 *     - ESP_OK: Successfully deinitialized the Thread Border Router.
 *     - Appropriate error code if the deinitialization fails.
 */
esp_err_t execute_thread_br_deinit_command(void);

#ifdef __cplusplus
}
#endif

#endif // THREAD_COMMANDS_H
