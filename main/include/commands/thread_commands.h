#ifndef THREAD_COMMANDS_H
#define THREAD_COMMANDS_H

#include <esp_event.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t execute_thread_network_init_command(uint16_t channel, uint16_t pan_id, const char *network_name,
                                              const char *extended_pan_id, const char *mesh_local_prefix,
                                              const char *network_key, const char *pskc);


esp_err_t execute_thread_enable_command();

esp_err_t execute_thread_disable_command();

#ifdef __cplusplus
}
#endif

#endif //THREAD_COMMANDS_H
