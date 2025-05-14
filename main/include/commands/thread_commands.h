#ifndef THREAD_COMMANDS_H
#define THREAD_COMMANDS_H

#include <esp_event.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Stack Control ----

/**
 * @brief Starts the Thread stack and brings the interface up.
 */
esp_err_t execute_thread_enable_command(void);

/**
 * @brief Stops the Thread stack and brings the interface down.
 */
esp_err_t execute_thread_disable_command(void);

// ---- Dataset ----

/**
 * @brief Initializes and sets the active Thread dataset.
 *
 * @param channel Radio channel
 * @param pan_id PAN ID
 * @param network_name Network name (string)
 * @param extended_pan_id Extended PAN ID (hex string, 16 chars)
 * @param mesh_local_prefix Mesh-local prefix (IPv6 string, e.g. "fd00::")
 * @param network_key Network master key (hex string, 32 chars)
 * @param pskc PSKc (hex string, 32 chars)
 */
esp_err_t execute_thread_dataset_init_command(uint16_t channel, uint16_t pan_id, const char *network_name,
                                              const char *extended_pan_id, const char *mesh_local_prefix,
                                              const char *network_key, const char *pskc);

// ---- Status / Monitoring ----

esp_err_t execute_thread_status_get_command(bool *is_running);

esp_err_t execute_thread_attached_get_command(bool *is_attached);

esp_err_t execute_thread_role_get_command(const char **role_str);

esp_err_t execute_thread_active_dataset_get_command(char *json_buf, size_t buf_size);

esp_err_t execute_thread_unicast_addresses_get_command(char **addresses, size_t max, size_t *count);

esp_err_t execute_thread_multicast_addresses_get_command(char **addresses, size_t max, size_t *count);

// ---- Border Router ----

esp_err_t execute_thread_br_init_command(void);

esp_err_t execute_thread_br_deinit_command(void);

#ifdef __cplusplus
}
#endif

#endif // THREAD_COMMANDS_H
