#include "control/temperature_control.h"

#include "commands/matter_commands.h"
#include "state/orchestrator_state.h"

#include <cinttypes>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>

static constexpr const char *TAG = "TEMP_CONTROL";
static constexpr const char *NVS_NAMESPACE = "temp_control";
static constexpr const char *NVS_KEY = "main_rule";
static constexpr uint32_t STORE_VERSION = 1;
static constexpr int64_t STALE_AFTER_US = 70LL * 1000LL * 1000LL;

struct StoredRule {
    uint32_t version;
    bool configured;
    bool enabled;
    char rule_id[DEVICE_REGISTRY_ID_MAX];
    char chamber_id[DEVICE_REGISTRY_ID_MAX];
    char sensor_device_id[DEVICE_REGISTRY_ID_MAX];
    char sensor_capability_id[DEVICE_REGISTRY_ID_MAX];
    char actuator_device_id[DEVICE_REGISTRY_ID_MAX];
    char actuator_capability_id[DEVICE_REGISTRY_ID_MAX];
    double min_celsius;
    double max_celsius;
};

struct LatestValues {
    bool has_temperature;
    double temperature_celsius;
    int raw_temperature;
    int64_t temperature_time_us;
    bool has_pressure;
    double pressure_kpa;
    int raw_pressure;
    int64_t pressure_time_us;
    bool relay_known;
    bool relay_on;
    bool last_command_known;
    bool last_commanded_on;
    char control_state[16];
    char last_error[64];
};

static SemaphoreHandle_t mutex = nullptr;
static StoredRule rule = {};
static LatestValues latest = {};

static void copy_field(char *dest, const char *src, size_t len) {
    if (!dest || len == 0) return;
    if (!src) src = "";
    strncpy(dest, src, len - 1);
    dest[len - 1] = '\0';
}

static void init_rule(StoredRule *target) {
    memset(target, 0, sizeof(*target));
    target->version = STORE_VERSION;
    copy_field(target->rule_id, "main-air-temperature-fan", sizeof(target->rule_id));
    copy_field(target->chamber_id, "main", sizeof(target->chamber_id));
    target->min_celsius = 24.0;
    target->max_celsius = 28.0;
}

static esp_err_t save_rule_locked(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, NVS_KEY, &rule, sizeof(rule));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t load_rule_locked(void) {
    init_rule(&rule);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    size_t len = sizeof(rule);
    err = nvs_get_blob(handle, NVS_KEY, &rule, &len);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        init_rule(&rule);
        return ESP_OK;
    }
    if (err != ESP_OK) return err;
    if (len != sizeof(rule) || rule.version != STORE_VERSION) {
        ESP_LOGW(TAG, "Resetting unsupported temperature rule blob");
        init_rule(&rule);
    }
    return ESP_OK;
}

static bool capability_id_matches(const DeviceCapability &capability, const char *capability_id) {
    return capability_id && capability_id[0] &&
           strncmp(capability.capability_id, capability_id, DEVICE_REGISTRY_ID_MAX) == 0;
}

static esp_err_t find_capability_by_id(const char *device_id,
                                       const char *capability_id,
                                       DeviceCapabilitySemanticType semantic,
                                       DeviceRecord *record,
                                       DeviceCapability *capability) {
    DeviceRecord found = {};
    ESP_RETURN_ON_ERROR(device_registry_get_device(device_id, &found), TAG, "device not found");
    for (uint32_t i = 0; i < found.capability_count; ++i) {
        if (found.capabilities[i].semantic_type == semantic &&
            capability_id_matches(found.capabilities[i], capability_id)) {
            if (record) *record = found;
            if (capability) *capability = found.capabilities[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static bool temp_is_stale_locked(void) {
    return !latest.has_temperature ||
           (esp_timer_get_time() - latest.temperature_time_us) > STALE_AFTER_US;
}

static cJSON *endpoint_ref_json(const char *device_id, const char *capability_id) {
    cJSON *item = cJSON_CreateObject();
    if (!item) return nullptr;
    cJSON_AddStringToObject(item, "device_id", device_id);
    cJSON_AddStringToObject(item, "capability_id", capability_id);
    return item;
}

static cJSON *rule_json_locked(void) {
    cJSON *item = cJSON_CreateObject();
    if (!item) return nullptr;
    cJSON_AddStringToObject(item, "rule_id", rule.rule_id);
    cJSON_AddStringToObject(item, "chamber_id", rule.chamber_id);
    cJSON_AddBoolToObject(item, "configured", rule.configured);
    cJSON_AddBoolToObject(item, "enabled", rule.enabled);
    cJSON_AddNumberToObject(item, "min_celsius", rule.min_celsius);
    cJSON_AddNumberToObject(item, "max_celsius", rule.max_celsius);
    cJSON_AddStringToObject(item, "mode", "cooling");
    cJSON_AddStringToObject(item, "state", latest.control_state[0] ? latest.control_state : (rule.enabled ? "idle" : "disabled"));
    if (latest.last_error[0]) cJSON_AddStringToObject(item, "last_error", latest.last_error);
    else cJSON_AddNullToObject(item, "last_error");
    cJSON_AddItemToObject(item, "sensor", endpoint_ref_json(rule.sensor_device_id, rule.sensor_capability_id));
    cJSON_AddItemToObject(item, "actuator", endpoint_ref_json(rule.actuator_device_id, rule.actuator_capability_id));
    return item;
}

static cJSON *chamber_json_locked(void) {
    cJSON *item = cJSON_CreateObject();
    if (!item) return nullptr;
    cJSON_AddStringToObject(item, "chamber_id", "main");
    cJSON_AddStringToObject(item, "label", "Main chamber");
    if (latest.has_temperature) cJSON_AddNumberToObject(item, "temperature_celsius", latest.temperature_celsius);
    else cJSON_AddNullToObject(item, "temperature_celsius");
    if (latest.has_pressure) cJSON_AddNumberToObject(item, "pressure_kpa", latest.pressure_kpa);
    else cJSON_AddNullToObject(item, "pressure_kpa");
    if (latest.relay_known || latest.last_command_known) {
        cJSON_AddBoolToObject(item, "relay_on", latest.relay_known ? latest.relay_on : latest.last_commanded_on);
    } else {
        cJSON_AddNullToObject(item, "relay_on");
    }
    cJSON_AddItemToObject(item, "temperature_source", endpoint_ref_json(rule.sensor_device_id, rule.sensor_capability_id));
    cJSON_AddItemToObject(item, "fan_actuator", endpoint_ref_json(rule.actuator_device_id, rule.actuator_capability_id));
    cJSON_AddItemToObject(item, "control", rule_json_locked());
    return item;
}

static esp_err_t emit_event(const char *event, cJSON *payload) {
    return orchestrator_state_broadcast_event(event, payload);
}

static esp_err_t evaluate_locked(bool force_if_unknown) {
    if (!rule.configured || !rule.enabled) {
        copy_field(latest.control_state, "disabled", sizeof(latest.control_state));
        return ESP_OK;
    }
    if (temp_is_stale_locked()) {
        copy_field(latest.control_state, "stale", sizeof(latest.control_state));
        emit_event("control.rule_stale", nullptr);
        return ESP_ERR_INVALID_STATE;
    }

    bool want_on;
    const char *threshold = nullptr;
    if (latest.temperature_celsius >= rule.max_celsius) {
        want_on = true;
        threshold = "max";
    } else if (latest.temperature_celsius <= rule.min_celsius) {
        want_on = false;
        threshold = "min";
    } else {
        copy_field(latest.control_state,
                   (latest.relay_known ? latest.relay_on : latest.last_commanded_on) ? "cooling" : "idle",
                   sizeof(latest.control_state));
        emit_event("control.rule_evaluated", nullptr);
        return ESP_OK;
    }

    const bool known_state = latest.relay_known || latest.last_command_known;
    const bool current_on = latest.relay_known ? latest.relay_on : latest.last_commanded_on;
    if (known_state && current_on == want_on && !force_if_unknown) {
        copy_field(latest.control_state, want_on ? "cooling" : "idle", sizeof(latest.control_state));
        emit_event("control.rule_evaluated", nullptr);
        return ESP_OK;
    }

    DeviceRecord actuator = {};
    DeviceCapability capability = {};
    esp_err_t err = find_capability_by_id(rule.actuator_device_id, rule.actuator_capability_id,
                                          DEVICE_CAPABILITY_RELAY, &actuator, &capability);
    if (err != ESP_OK) {
        copy_field(latest.control_state, "error", sizeof(latest.control_state));
        copy_field(latest.last_error, "Actuator capability not found", sizeof(latest.last_error));
        emit_event("control.rule_error", nullptr);
        return err;
    }

    err = execute_cmd_invoke_command(actuator.node_id, capability.endpoint_id, capability.cluster_id,
                                     want_on ? 0x01 : 0x00, "{}");
    if (err != ESP_OK) {
        copy_field(latest.control_state, "error", sizeof(latest.control_state));
        copy_field(latest.last_error, "Matter invoke failed", sizeof(latest.last_error));
        emit_event("control.rule_error", nullptr);
        return err;
    }

    latest.last_command_known = true;
    latest.last_commanded_on = want_on;
    copy_field(latest.control_state, want_on ? "cooling" : "idle", sizeof(latest.control_state));
    latest.last_error[0] = '\0';

    cJSON *payload = cJSON_CreateObject();
    if (payload) {
        cJSON_AddStringToObject(payload, "rule_id", rule.rule_id);
        cJSON_AddNumberToObject(payload, "temperature_celsius", latest.temperature_celsius);
        cJSON_AddStringToObject(payload, "threshold", threshold);
        cJSON_AddStringToObject(payload, "command", want_on ? "on" : "off");
        cJSON_AddItemToObject(payload, "actuator", endpoint_ref_json(rule.actuator_device_id, rule.actuator_capability_id));
    }
    emit_event("control.rule_action", payload);
    orchestrator_state_broadcast_snapshot();
    return ESP_OK;
}

esp_err_t temperature_control_init(void) {
    if (!mutex) {
        mutex = xSemaphoreCreateMutex();
        if (!mutex) return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    memset(&latest, 0, sizeof(latest));
    copy_field(latest.control_state, "disabled", sizeof(latest.control_state));
    esp_err_t err = load_rule_locked();
    xSemaphoreGive(mutex);
    return err;
}

esp_err_t temperature_control_upsert_rule(const char *rule_id,
                                          const char *chamber_id,
                                          bool enabled,
                                          const char *sensor_device_id,
                                          const char *sensor_capability_id,
                                          const char *actuator_device_id,
                                          const char *actuator_capability_id,
                                          double min_celsius,
                                          double max_celsius) {
    if (!sensor_device_id || !sensor_capability_id || !actuator_device_id || !actuator_capability_id ||
        !(min_celsius < max_celsius) || min_celsius < -40.0 || max_celsius > 85.0) {
        return ESP_ERR_INVALID_ARG;
    }
    DeviceRecord sensor = {};
    DeviceCapability sensor_cap = {};
    ESP_RETURN_ON_ERROR(find_capability_by_id(sensor_device_id, sensor_capability_id,
                                              DEVICE_CAPABILITY_TEMPERATURE, &sensor, &sensor_cap),
                        TAG, "invalid sensor capability");
    DeviceRecord actuator = {};
    DeviceCapability actuator_cap = {};
    ESP_RETURN_ON_ERROR(find_capability_by_id(actuator_device_id, actuator_capability_id,
                                              DEVICE_CAPABILITY_RELAY, &actuator, &actuator_cap),
                        TAG, "invalid actuator capability");

    xSemaphoreTake(mutex, portMAX_DELAY);
    rule.configured = true;
    rule.enabled = enabled;
    copy_field(rule.rule_id, rule_id && rule_id[0] ? rule_id : "main-air-temperature-fan", sizeof(rule.rule_id));
    copy_field(rule.chamber_id, chamber_id && chamber_id[0] ? chamber_id : "main", sizeof(rule.chamber_id));
    copy_field(rule.sensor_device_id, sensor_device_id, sizeof(rule.sensor_device_id));
    copy_field(rule.sensor_capability_id, sensor_capability_id, sizeof(rule.sensor_capability_id));
    copy_field(rule.actuator_device_id, actuator_device_id, sizeof(rule.actuator_device_id));
    copy_field(rule.actuator_capability_id, actuator_capability_id, sizeof(rule.actuator_capability_id));
    rule.min_celsius = min_celsius;
    rule.max_celsius = max_celsius;
    copy_field(latest.control_state, enabled ? "idle" : "disabled", sizeof(latest.control_state));
    esp_err_t err = save_rule_locked();
    if (err == ESP_OK && enabled) {
        execute_attr_subscribe_command(sensor.node_id, sensor_cap.endpoint_id, sensor_cap.cluster_id,
                                       sensor_cap.attribute_id, 2, 30);
        evaluate_locked(true);
    }
    xSemaphoreGive(mutex);
    emit_event("control.rule_configured", nullptr);
    orchestrator_state_broadcast_snapshot();
    return err;
}

esp_err_t temperature_control_get_rule(cJSON **out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    *out = rule_json_locked();
    xSemaphoreGive(mutex);
    return *out ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t temperature_control_set_enabled(bool enabled) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!rule.configured) {
        xSemaphoreGive(mutex);
        return ESP_ERR_NOT_FOUND;
    }
    rule.enabled = enabled;
    copy_field(latest.control_state, enabled ? "idle" : "disabled", sizeof(latest.control_state));
    esp_err_t err = save_rule_locked();
    if (err == ESP_OK && enabled) evaluate_locked(true);
    xSemaphoreGive(mutex);
    emit_event(enabled ? "control.rule_enabled" : "control.rule_disabled", nullptr);
    orchestrator_state_broadcast_snapshot();
    return err;
}

esp_err_t temperature_control_delete_rule(void) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    init_rule(&rule);
    copy_field(latest.control_state, "disabled", sizeof(latest.control_state));
    esp_err_t err = save_rule_locked();
    xSemaphoreGive(mutex);
    emit_event("control.rule_deleted", nullptr);
    orchestrator_state_broadcast_snapshot();
    return err;
}

esp_err_t temperature_control_handle_attribute_report(uint64_t node_id,
                                                      uint16_t endpoint_id,
                                                      uint32_t cluster_id,
                                                      uint32_t attribute_id,
                                                      const char *value) {
    DeviceRecord record = {};
    DeviceCapability capability = {};
    if (device_registry_find_capability_by_path(node_id, endpoint_id, cluster_id, attribute_id,
                                                &record, &capability) != ESP_OK) {
        return ESP_OK;
    }
    const int raw_value = value ? atoi(value) : 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (capability.semantic_type == DEVICE_CAPABILITY_TEMPERATURE) {
        latest.has_temperature = true;
        latest.raw_temperature = raw_value;
        latest.temperature_celsius = raw_value / 100.0;
        latest.temperature_time_us = esp_timer_get_time();
        if (rule.configured &&
            strncmp(record.device_id, rule.sensor_device_id, DEVICE_REGISTRY_ID_MAX) == 0 &&
            strncmp(capability.capability_id, rule.sensor_capability_id, DEVICE_REGISTRY_ID_MAX) == 0) {
            evaluate_locked(false);
        }
    } else if (capability.semantic_type == DEVICE_CAPABILITY_PRESSURE) {
        latest.has_pressure = true;
        latest.raw_pressure = raw_value;
        latest.pressure_kpa = raw_value / 10.0;
        latest.pressure_time_us = esp_timer_get_time();
    } else if (capability.semantic_type == DEVICE_CAPABILITY_RELAY) {
        latest.relay_known = true;
        latest.relay_on = raw_value != 0;
    }
    xSemaphoreGive(mutex);
    orchestrator_state_broadcast_snapshot();
    return ESP_OK;
}

esp_err_t temperature_control_add_snapshot_fields(cJSON *root) {
    if (!root) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    cJSON *chambers = cJSON_AddArrayToObject(root, "chambers");
    if (chambers) cJSON_AddItemToArray(chambers, chamber_json_locked());
    cJSON *rules = cJSON_AddArrayToObject(root, "control_rules");
    if (rules && rule.configured) cJSON_AddItemToArray(rules, rule_json_locked());
    xSemaphoreGive(mutex);
    return ESP_OK;
}
