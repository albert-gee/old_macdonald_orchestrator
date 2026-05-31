#ifndef TEMPERATURE_CONTROL_H
#define TEMPERATURE_CONTROL_H

#include "registry/device_registry.h"

#include <cJSON.h>
#include <esp_err.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t temperature_control_init(void);
esp_err_t temperature_control_upsert_rule(const char *rule_id,
                                          const char *chamber_id,
                                          bool enabled,
                                          const char *sensor_device_id,
                                          const char *sensor_capability_id,
                                          const char *actuator_device_id,
                                          const char *actuator_capability_id,
                                          double min_celsius,
                                          double max_celsius);
esp_err_t temperature_control_get_rule(cJSON **out);
esp_err_t temperature_control_set_enabled(bool enabled);
esp_err_t temperature_control_delete_rule(void);
esp_err_t temperature_control_handle_attribute_report(uint64_t node_id,
                                                      uint16_t endpoint_id,
                                                      uint32_t cluster_id,
                                                      uint32_t attribute_id,
                                                      const char *value);
esp_err_t temperature_control_add_snapshot_fields(cJSON *root);

#ifdef __cplusplus
}
#endif

#endif
