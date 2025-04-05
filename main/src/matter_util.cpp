#include "matter_util.h"
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
#include <events/chip_event_handler.h>

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

esp_err_t invoke_cluster_command(uint64_t destination_id, uint16_t endpoint_id, uint32_t cluster_id,
                                 uint32_t command_id, const char *command_data_field) {
    ESP_LOGI(TAG, "Sending cluster invoke command");

    // Lock the Chip stack before interaction
    esp_matter::lock::status_t lock_status = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    if (lock_status != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Failed to lock Chip stack");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_matter::controller::send_invoke_cluster_command(destination_id, endpoint_id, cluster_id,
                                                                        command_id, command_data_field);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send invoke command: %s", esp_err_to_name(err));
        // Unlock the Chip stack before returning
        esp_matter::lock::chip_stack_unlock();
        return err;
    }

    ESP_LOGI(TAG, "Cluster invoke command sent successfully");

    // Unlock the Chip stack after interaction
    esp_matter::lock::chip_stack_unlock();
    return ESP_OK;
}

esp_err_t send_subscribe_attr_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, uint16_t min_interval, uint16_t max_interval,
                                      bool auto_resubscribe,
                                      esp_matter::controller::attribute_report_cb_t attribute_cb,
                                      esp_matter::controller::subscribe_done_cb_t done_cb)
{
    ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
    attr_paths.Alloc(1);
    if (!attr_paths.Get()) {
        ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
        return ESP_ERR_NO_MEM;
    }
    attr_paths[0] = AttributePathParams(endpoint_id, cluster_id, attribute_id);

    ScopedMemoryBufferWithSize<EventPathParams> event_paths;
    event_paths.Alloc(0); // Not used

    esp_matter::lock::status_t lock_status = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    if (lock_status != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Failed to lock Chip stack");
        return ESP_ERR_INVALID_STATE;
    }

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

    esp_err_t err = cmd->send_command();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send subscribe attr command: %s", esp_err_to_name(err));
        esp_matter::lock::chip_stack_unlock();
        return err;
    }

    ESP_LOGI(TAG, "Subscribe attr command sent successfully");
    esp_matter::lock::chip_stack_unlock();
    return ESP_OK;
}



esp_err_t send_read_attr_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id) {
    ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
    ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
    ScopedMemoryBufferWithSize<uint32_t> attribute_ids;
    endpoint_ids.Alloc(1);
    cluster_ids.Alloc(1);
    attribute_ids.Alloc(1);

    if (!(endpoint_ids.Get() && cluster_ids.Get() && attribute_ids.Get())) {
        return ESP_ERR_NO_MEM;
    }

    *endpoint_ids.Get() = endpoint_id;
    *cluster_ids.Get() = cluster_id;
    *attribute_ids.Get() = attribute_id;

    if (endpoint_ids.AllocatedSize() != cluster_ids.AllocatedSize() ||
        endpoint_ids.AllocatedSize() != attribute_ids.AllocatedSize()) {
        ESP_LOGE(TAG, "Mismatched array sizes for attribute read");
        return ESP_ERR_INVALID_ARG;
    }

    ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
    ScopedMemoryBufferWithSize<EventPathParams> event_paths;
    attr_paths.Alloc(endpoint_ids.AllocatedSize());
    if (!attr_paths.Get()) {
        ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < attr_paths.AllocatedSize(); ++i) {
        attr_paths[i] = AttributePathParams(endpoint_ids[i], cluster_ids[i], attribute_ids[i]);
    }

    if (esp_matter::lock::chip_stack_lock(portMAX_DELAY) != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Failed to lock CHIP stack");
        return ESP_ERR_INVALID_STATE;
    }

    auto *cmd = chip::Platform::New<esp_matter::controller::read_command>(
        node_id, std::move(attr_paths), std::move(event_paths),
        read_attribute_data_callback, nullptr, nullptr);

    esp_err_t err = ESP_OK;
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to alloc memory for read_command");
        err = ESP_ERR_NO_MEM;
    } else {
        err = cmd->send_command();
    }

    esp_matter::lock::chip_stack_unlock();

    return err;
}
