#ifndef MATTER_UTIL_H
#define MATTER_UTIL_H
#include <esp_err.h>

// #include <esp_matter.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pairing_ble_thread(char *node_id, char *pin, char *discriminator);

esp_err_t invoke_cluster_command(uint64_t destination_id, uint16_t endpoint_id, uint32_t cluster_id,
                                 uint32_t command_id, const char *command_data_field);

esp_err_t send_read_attr_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id);

esp_err_t send_subscribe_attr_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, uint16_t min_interval, uint16_t max_interval,
                                      bool auto_resubscribe);

#ifdef __cplusplus
}
#endif
#endif //MATTER_UTIL_H
