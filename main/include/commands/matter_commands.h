#ifndef COMMANDS_H
#define COMMANDS_H

#include <esp_event.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    esp_err_t execute_matter_controller_init_command(uint64_t node_id, uint64_t fabric_id, uint16_t listen_port);

    esp_err_t execute_matter_pair_ble_thread_command(uint64_t node_id, uint32_t pin, uint16_t discriminator);

    esp_err_t execute_invoke_cmd_command(uint64_t destination_id, uint16_t endpoint_id, uint32_t cluster_id,
                                             uint32_t command_id, const char *payload_json);

    esp_err_t execute_read_attr_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id,
                                            uint32_t attribute_id);

    esp_err_t execute_subscribe_attr_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id,
                                                 uint32_t attribute_id, uint16_t min_interval, uint16_t max_interval);


#ifdef __cplusplus
}
#endif

#endif //COMMANDS_H
