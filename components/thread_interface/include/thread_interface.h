#ifndef THREAD_INTERFACE_H
#define THREAD_INTERFACE_H

#include <esp_err.h>
#include "esp_event_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize OpenThread: create netif, attach glue, init stack, spawn worker.
 * @return ESP_OK on success or appropriate esp_err_t on failure.
 */
esp_err_t thread_interface_init(esp_event_handler_t event_handler);

#ifdef __cplusplus
}
#endif

#endif //THREAD_INTERFACE_H
