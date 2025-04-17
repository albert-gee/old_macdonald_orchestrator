#ifndef THREAD_INTERFACE_H
#define THREAD_INTERFACE_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize OpenThread: create netif, attach glue, init stack, spawn worker.
 * @return ESP_OK on success or appropriate esp_err_t on failure.
 */
esp_err_t thread_interface_init();

#ifdef __cplusplus
}
#endif

#endif //THREAD_INTERFACE_H
