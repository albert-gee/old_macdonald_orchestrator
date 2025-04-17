#ifndef THREAD_UTIL_H
#define THREAD_UTIL_H

#include <esp_err.h>
#include <openthread/dataset.h>
#include <openthread/instance.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes a new OpenThread dataset.
 *
 * This function sets up an OpenThread dataset with the specified parameters and
 * applies it to the OpenThread instance.
 *
 * @param channel The radio channel for the OpenThread network.
 * @param pan_id The PAN ID of the OpenThread network.
 * @param network_name The name of the OpenThread network.
 * @param extended_pan_id The Extended PAN ID in hex string format.
 * @param mesh_local_prefix The Mesh-Local Prefix of the OpenThread network.
 * @param network_key The network master key in hex string format.
 * @param pskc The Pre-Shared Commissioner Key (PSKc) in hex string format.
 *
 * @return ESP_OK on success, an `esp_err_t` error code otherwise.
 */
esp_err_t thread_dataset_init(uint16_t channel, uint16_t pan_id, const char *network_name,
                              const char *extended_pan_id, const char *mesh_local_prefix,
                              const char *network_key, const char *pskc);

/**
 * @brief Enables the OpenThread IPv6 interface.
 *
 * This function enables the IPv6 interface for OpenThread, allowing
 * the device to communicate using IPv6 over Thread.
 *
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t ifconfig_up(void);

/**
 * @brief Disables the OpenThread IPv6 interface.
 *
 * This function disables the IPv6 interface for OpenThread.
 *
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t ifconfig_down(void);

/**
 * @brief Starts the OpenThread stack.
 *
 * This function enables the OpenThread stack, allowing the device to
 * participate in a Thread network.
 *
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t thread_start(void);

/**
 * @brief Stops the OpenThread stack.
 *
 * This function disables the OpenThread stack, disconnecting the device
 * from the Thread network.
 *
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t thread_stop(void);

esp_err_t thread_get_device_role_name(char *device_role_name);

esp_err_t thread_get_active_dataset(otOperationalDataset *dataset);
esp_err_t thread_get_active_dataset_tlvs(uint8_t *dataset_tlvs, uint8_t *dataset_len);

esp_err_t thread_br_init();

#ifdef __cplusplus
}
#endif

#endif // THREAD_UTIL_H
