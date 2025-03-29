// #include "matter_util.h"
// #include <esp_check.h>
// #include <esp_err.h>
// #include "esp_matter_controller_console.h"
// #include <inttypes.h>
// #include <esp_log.h>
// #include <esp_matter_controller_pairing_command.h>
// #include <thread_util.h>
// #include <cstdlib>
//
// static const char *TAG = "matter_util";
//
// esp_err_t pairing_ble_thread(char *node_id, char *pin, char *discriminator)
// {
//     // Validate input parameters
//     if (!node_id || !pin || !discriminator) {
//         ESP_LOGE(TAG, "Invalid arguments");
//         return ESP_ERR_INVALID_ARG;
//     }
//
//     ESP_LOGI(TAG, "Starting BLE Thread pairing with node_id=%s, pin=%s, discriminator=%s",
//              node_id, pin, discriminator);
//
//     // Convert string parameters
//     uint64_t node_id_val;
//     uint32_t pin_val;
//     uint16_t discriminator_val;
//
//     // Parse node ID (handle hex format)
//     node_id_val = strtoull(node_id, nullptr, 0);
//     if (node_id_val == 0 && errno == EINVAL) {
//         ESP_LOGE(TAG, "Invalid node_id format");
//         return ESP_ERR_INVALID_ARG;
//     }
//
//     // Parse pin (decimal)
//     pin_val = strtoul(pin, nullptr, 10);
//     if (pin_val == 0 && errno == EINVAL) {
//         ESP_LOGE(TAG, "Invalid pin format");
//         return ESP_ERR_INVALID_ARG;
//     }
//
//     // Parse discriminator (decimal)
//     discriminator_val = strtoul(discriminator, nullptr, 10);
//     if (discriminator_val == 0 && errno == EINVAL) {
//         ESP_LOGE(TAG, "Invalid discriminator format");
//         return ESP_ERR_INVALID_ARG;
//     }
//
//     ESP_LOGI(TAG, "Parsed values: node_id=0x%" PRIX64 ", pin=%" PRIu32 ", discriminator=%" PRIu16,
//              node_id_val, pin_val, discriminator_val);
//
//     // Get Thread dataset in TLV format
//     uint8_t dataset_tlvs[OT_OPERATIONAL_DATASET_MAX_LENGTH];
//     uint8_t dataset_len = sizeof(dataset_tlvs);
//
//     esp_err_t err = thread_get_active_dataset_tlvs(dataset_tlvs, &dataset_len);
//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to get Thread dataset: %s", esp_err_to_name(err));
//         return err;
//     }
//
//     ESP_LOGI(TAG, "Thread dataset length: %d", dataset_len);
//
//     // Start pairing - no heap allocation
//     ESP_LOGI(TAG, "Starting BLE Thread pairing with node 0x%" PRIX64, node_id_val);
//
//     return esp_matter::controller::pairing_ble_thread(
//         static_cast<chip::NodeId>(node_id_val),
//         pin_val,
//         discriminator_val,
//         dataset_tlvs,
//         dataset_len);
// }