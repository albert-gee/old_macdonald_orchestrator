#include "matter_util.h"
#include <esp_check.h>
#include <esp_err.h>
#include <inttypes.h>
#include <esp_log.h>
#include <esp_matter_controller_pairing_command.h>
#include <thread_util.h>
#include <cstdlib>
#include <cerrno>
#include <esp_matter_controller_cluster_command.h>
#include <esp_matter_controller_read_command.h>
#include <esp_matter_controller_subscribe_command.h>
#include <esp_matter_controller_utils.h>

static const char *TAG = "matter_util";

esp_err_t pairing_ble_thread(char *node_id, char *pin, char *discriminator) {
    // Validate input parameters
    if (!node_id || !pin || !discriminator) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting BLE Thread pairing with node_id=%s, pin=%s, discriminator=%s",
             node_id, pin, discriminator);

    // Reset errno before parsing
    errno = 0;
    uint64_t node_id_val = strtoull(node_id, nullptr, 0);
    if (errno != 0 || node_id_val == 0) {
        ESP_LOGE(TAG, "Invalid node_id format");
        return ESP_ERR_INVALID_ARG;
    }

    errno = 0;
    uint32_t pin_val = strtoul(pin, nullptr, 10);
    if (errno != 0 || pin_val == 0) {
        ESP_LOGE(TAG, "Invalid pin format");
        return ESP_ERR_INVALID_ARG;
    }

    errno = 0;
    uint16_t discriminator_val = strtoul(discriminator, nullptr, 10);
    if (errno != 0 || discriminator_val == 0) {
        ESP_LOGE(TAG, "Invalid discriminator format");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Parsed values: node_id=0x%" PRIX64 ", pin=%" PRIu32 ", discriminator=%" PRIu16,
             node_id_val, pin_val, discriminator_val);

    // Get Thread dataset in TLV format
    uint8_t dataset_tlvs[OT_OPERATIONAL_DATASET_MAX_LENGTH];
    uint8_t dataset_len = sizeof(dataset_tlvs);

    esp_err_t err = thread_get_active_dataset_tlvs(dataset_tlvs, &dataset_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get Thread dataset TLVs");
        return err;
    }
    ESP_LOGI(TAG, "Thread dataset length: %zu", dataset_len);

    // Start pairing
    ESP_LOGI(TAG, "Starting BLE Thread pairing with node 0x%" PRIX64, node_id_val);

    return esp_matter::controller::pairing_ble_thread(
        static_cast<chip::NodeId>(node_id_val),
        pin_val,
        discriminator_val,
        dataset_tlvs,
        dataset_len);
}


// // Custom success callback function
// static void on_success(void *ctx, const chip::app::ConcreteCommandPath &command_path, const chip::app::StatusIB &status, chip::TLV::TLVReader *response_data) {
//     ESP_LOGI(TAG, "Cluster command succeeded");
// }
//
// // Custom error callback function
// static void on_error(void *ctx, CHIP_ERROR error) {
//     ESP_LOGE(TAG, "Cluster command failed");
// }

esp_err_t invoke_cluster_command(uint64_t destination_id, uint16_t endpoint_id, uint32_t cluster_id,
                                 uint32_t command_id, const char *command_data_field) {
    ESP_LOGI(TAG, "Sending cluster invoke command");

    // Lock the Chip stack before interaction
    esp_matter::lock::status_t lock_status = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    if (lock_status != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Failed to lock Chip stack");
        return ESP_ERR_INVALID_STATE;
    }
    //
    //
    // esp_err_t err = esp_matter::controller::send_invoke_cluster_command(destination_id, endpoint_id, cluster_id,
    //                                   command_id, command_data_field);
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to send invoke command: %s", esp_err_to_name(err));
    //     // Unlock the Chip stack before returning
    //     esp_matter::lock::chip_stack_unlock();
    //     return err;
    // }


    // uint64_t node_id1 = uint64_t(0x1122) ;
    // uint16_t endpoint_id1 = uint16_t(1);
    // uint32_t cluster_id1 = uint32_t(6);
    // uint32_t command_id1 = uint32_t(0);
    // const char *command_data_field1 = "{}";
    // esp_err_t err = esp_matter::controller::send_invoke_cluster_command(node_id1, endpoint_id1, cluster_id1,
    //                                                                     command_id1, command_data_field1);
    esp_err_t err = esp_matter::controller::send_invoke_cluster_command(destination_id, endpoint_id, cluster_id,
                                                                        command_id, command_data_field);


    // uint64_t node_id1 = string_to_uint64("0x1122");
    // uint16_t endpoint_id1 = string_to_uint16("1");
    // uint32_t cluster_id1 = string_to_uint32("6");
    // uint32_t command_id1 = string_to_uint32("1");
    //
    // ESP_LOGI(TAG, "Parsed values: node_id=0x%" PRIX64 ", endpoint_id=%" PRIu16 ", cluster_id=%" PRIu32 ", command_id=%" PRIu32,
    //          node_id1, endpoint_id1, cluster_id1, command_id1);
    //
    // esp_err_t err = esp_matter::controller::send_invoke_cluster_command(node_id1, endpoint_id1, cluster_id1, command_id1, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send invoke command: %s", esp_err_to_name(err));
        // Unlock the Chip stack before returning
        esp_matter::lock::chip_stack_unlock();
        return err;
    }


    // Create cluster command object with custom success and error callbacks
    // esp_matter::controller::cluster_command clusterCmd(
    //     destination_id,
    //     endpoint_id,
    //     cluster_id,
    //     command_id,
    //     command_data_field,
    //     chip::NullOptional, // No timeout
    //     on_success,         // Custom success callback
    //     on_error            // Custom error callback
    // );
    //
    // // Send the command
    // esp_err_t err = clusterCmd.send_command();
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to send invoke command: %s", esp_err_to_name(err));
    //     // esp_matter::lock::chip_stack_unlock();
    //     return err;
    // }

    ESP_LOGI(TAG, "Cluster invoke command sent successfully");

    // Unlock the Chip stack after interaction
    esp_matter::lock::chip_stack_unlock();
    return ESP_OK;
}

esp_err_t send_read_attr_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id) {
    ESP_LOGI(TAG, "Sending read attr command");

    // Lock the Chip stack before interaction
    esp_matter::lock::status_t lock_status = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    if (lock_status != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Failed to lock Chip stack");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_matter::controller::send_read_attr_command(node_id, endpoint_id, cluster_id, attribute_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send read attr command: %s", esp_err_to_name(err));
        // Unlock the Chip stack before returning
        esp_matter::lock::chip_stack_unlock();
        return err;
    }
    ESP_LOGI(TAG, "Read attr command sent successfully");

    // Unlock the Chip stack after interaction
    esp_matter::lock::chip_stack_unlock();
    return ESP_OK;
}

esp_err_t send_subscribe_attr_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, uint16_t min_interval, uint16_t max_interval,
                                      bool auto_resubscribe) {
    ESP_LOGI(TAG, "Sending subscribe attr command");

    // Lock the Chip stack before interaction
    esp_matter::lock::status_t lock_status = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    if (lock_status != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Failed to lock Chip stack");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_matter::controller::send_subscribe_attr_command(node_id, endpoint_id, cluster_id, attribute_id,
                                                                        min_interval, max_interval, auto_resubscribe);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send subscribe attr command: %s", esp_err_to_name(err));
        // Unlock the Chip stack before returning
        esp_matter::lock::chip_stack_unlock();
        return err;
    }
    ESP_LOGI(TAG, "Subscribe attr command sent successfully");

    // Unlock the Chip stack after interaction
    esp_matter::lock::chip_stack_unlock();
    return ESP_OK;
}
