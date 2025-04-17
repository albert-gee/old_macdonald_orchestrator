#include "matter_util.h"
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_client.h>
#include <portmacro.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_controller_cluster_command.h>
#include <esp_matter_controller_read_command.h>
#include <esp_matter_controller_subscribe_command.h>
#include <esp_matter_controller_utils.h>

static const char *TAG = "MATTER_UTIL";

esp_err_t pairing_ble_thread(const uint64_t node_id, const uint32_t pin, const uint16_t discriminator,
                             uint8_t *dataset_tlvs,
                             const size_t dataset_len) {
    // Validate dataset parameters
    if (dataset_len > 0 && !dataset_tlvs) {
        ESP_LOGE(TAG, "Invalid dataset parameters");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting BLE Thread pairing with node 0x%" PRIX64, node_id);
    return esp_matter::controller::pairing_ble_thread(node_id, pin, discriminator, dataset_tlvs, dataset_len);
}

esp_err_t invoke_cluster_command(uint64_t destination_id, uint16_t endpoint_id, uint32_t cluster_id,
                                 uint32_t command_id, const char *command_data_field) {
    if (!command_data_field) {
        ESP_LOGE(TAG, "Invalid command data field");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending cluster invoke command");

    // Lock the CHIP stack for thread-safe access
    esp_matter::lock::status_t lock_status = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    if (lock_status != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Failed to lock Chip stack");
        return ESP_ERR_INVALID_STATE;
    }

    // Send the command
    esp_err_t err = esp_matter::controller::send_invoke_cluster_command(destination_id, endpoint_id, cluster_id,
                                                                        command_id, command_data_field);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send invoke command: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Cluster invoke command sent successfully");
    }

    // Unlock the CHIP stack
    esp_matter::lock::chip_stack_unlock();

    return err;
}

esp_err_t send_subscribe_attr_command(uint64_t node_id, const uint16_t endpoint_id, const uint32_t cluster_id,
                                      const uint32_t attribute_id, uint16_t min_interval, uint16_t max_interval,
                                      bool auto_resubscribe,
                                      esp_matter::controller::attribute_report_cb_t attribute_cb,
                                      esp_matter::controller::subscribe_done_cb_t done_cb) {
    // Allocate memory for attribute path
    ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
    attr_paths.Alloc(1);
    if (!attr_paths.Get()) {
        ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
        return ESP_ERR_NO_MEM;
    }
    attr_paths[0] = AttributePathParams(endpoint_id, cluster_id, attribute_id);

    // Empty event path array (not subscribing to events)
    ScopedMemoryBufferWithSize<EventPathParams> event_paths;
    event_paths.Alloc(0);

    // Lock CHIP stack before creating command
    esp_matter::lock::status_t lock_status = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    if (lock_status != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Failed to lock Chip stack");
        return ESP_ERR_INVALID_STATE;
    }

    // Create and initialize the subscription command
    auto *cmd = chip::Platform::New<esp_matter::controller::subscribe_command>(
        node_id,
        std::move(attr_paths),
        std::move(event_paths),
        min_interval,
        max_interval,
        auto_resubscribe,
        attribute_cb,
        nullptr,
        done_cb,
        nullptr
    );
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to alloc memory for subscribe_command");
        esp_matter::lock::chip_stack_unlock();
        return ESP_ERR_NO_MEM;
    }

    // Send the subscription command
    esp_err_t err = cmd->send_command();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send subscribe attr command: %s", esp_err_to_name(err));
        chip::Platform::Delete(cmd);
    } else {
        ESP_LOGI(TAG, "Subscribe attr command sent successfully");
    }

    // Unlock the CHIP stack
    esp_matter::lock::chip_stack_unlock();

    return err;
}

esp_err_t send_read_attr_command(uint64_t node_id, const uint16_t endpoint_id, const uint32_t cluster_id,
                                 const uint32_t attribute_id, void (*read_attribute_data_callback)(
                                     uint64_t, const chip::app::ConcreteDataAttributePath &, TLVReader *)) {
    if (!read_attribute_data_callback) {
        ESP_LOGE(TAG, "Invalid read attribute callback");
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate memory for attribute path
    ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
    attr_paths.Alloc(1);
    if (!attr_paths.Get()) {
        ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
        return ESP_ERR_NO_MEM;
    }
    attr_paths[0] = AttributePathParams(endpoint_id, cluster_id, attribute_id);

    // Empty event path array (not reading events)
    ScopedMemoryBufferWithSize<EventPathParams> event_paths;
    event_paths.Alloc(0);

    // Lock CHIP stack
    if (esp_matter::lock::chip_stack_lock(portMAX_DELAY) != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Failed to lock CHIP stack");
        return ESP_ERR_INVALID_STATE;
    }

    // Create and initialize the read command
    esp_err_t err = ESP_OK;
    auto *cmd = chip::Platform::New<esp_matter::controller::read_command>(
        node_id, std::move(attr_paths), std::move(event_paths),
        read_attribute_data_callback, nullptr, nullptr);
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to alloc memory for read_command");
        err = ESP_ERR_NO_MEM;
    } else {
        err = cmd->send_command();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send read command: %s", esp_err_to_name(err));
            chip::Platform::Delete(cmd);
        }
    }

    // Unlock the CHIP stack
    esp_matter::lock::chip_stack_unlock();

    return err;
}
