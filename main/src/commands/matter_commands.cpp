#include "commands/matter_commands.h"

#include "matter_interface.h"
#include "matter_controller.h"
#include "thread_util.h"

#include <cstring>

#include "event_handlers/chip_event_handler.h"

esp_err_t execute_matter_pair_ble_thread_command(const uint64_t node_id, const uint32_t pin,
                                              const uint16_t discriminator) {
    uint8_t dataset_len = OT_OPERATIONAL_DATASET_MAX_LENGTH;
    auto *tlvs = static_cast<uint8_t *>(malloc(dataset_len));
    if (!tlvs) return ESP_ERR_NO_MEM;

    esp_err_t err = thread_get_active_dataset_tlvs(tlvs, &dataset_len);
    if (err != ESP_OK) {
        free(tlvs);
        return err;
    }

    return pairing_ble_thread(node_id, pin, discriminator, tlvs, dataset_len);
}

esp_err_t execute_cmd_invoke_command(const uint64_t destination_id, const uint16_t endpoint_id,
                                         const uint32_t cluster_id,
                                         const uint32_t command_id, const char *payload_json) {
    return invoke_cluster_command(destination_id, endpoint_id, cluster_id, command_id, payload_json);
}

esp_err_t execute_attr_read_command(uint64_t node_id, const uint16_t endpoint_id, const uint32_t cluster_id,
                                 const uint32_t attribute_id) {
    return send_read_attr_command(node_id, endpoint_id, cluster_id, attribute_id);
}

esp_err_t execute_attr_subscribe_command(uint64_t node_id, const uint16_t endpoint_id, const uint32_t cluster_id,
                                      const uint32_t attribute_id, uint16_t min_interval, uint16_t max_interval) {
    return send_subscribe_attr_command(node_id, endpoint_id, cluster_id, attribute_id, min_interval, max_interval, true);
}

esp_err_t execute_matter_controller_init_command(const uint64_t node_id, const uint64_t fabric_id, const uint16_t listen_port) {
    return matter_controller_init(node_id, fabric_id, listen_port, attribute_data_report_callback, subscribe_done_callback);
}
