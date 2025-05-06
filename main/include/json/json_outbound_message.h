#ifndef JSON_OUTBOUND_MESSAGE_H
#define JSON_OUTBOUND_MESSAGE_H

#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t create_info_wifi_status_message(const char *status);

esp_err_t create_info_thread_role_message(const char *role);

esp_err_t create_info_ifconfig_status_message(const char *status);

esp_err_t create_info_thread_status_message(const char *status);

esp_err_t create_info_sensor_temperature_message(const char *value);

esp_err_t create_info_thread_dataset_active_message(uint64_t active_timestamp, uint64_t pending_timestamp,
                                                    const uint8_t *network_key, const char *network_name,
                                                    const uint8_t *extended_pan_id, const uint8_t *mesh_local_prefix,
                                                    uint32_t delay, uint16_t pan_id, uint16_t channel,
                                                    uint16_t wakeup_channel, const uint8_t *pskc,
                                                    size_t mesh_local_prefix_size);

esp_err_t create_info_matter_commissioning_complete_message(uint64_t nodeId, uint8_t fabricIndex);

esp_err_t create_info_matter_attribute_report_message(uint64_t nodeId, uint16_t endpointId, uint32_t clusterId,
                                                      uint32_t attributeId, char *value);

esp_err_t create_info_matter_subscribe_done_message(uint64_t nodeId, uint32_t subscription_id);

#ifdef __cplusplus
}
#endif

#endif // JSON_OUTBOUND_MESSAGE_H
