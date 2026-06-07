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
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <nvs.h>

static constexpr const char *TAG = "TEMP_CONTROL";
static constexpr const char *NVS_NAMESPACE = "temp_control";
static constexpr const char *NVS_KEY = "main_rule";
static constexpr uint32_t STORE_VERSION = 1;
static constexpr int64_t STALE_AFTER_US = 70LL * 1000LL * 1000LL;
static constexpr int64_t FALLBACK_POLL_INTERVAL_US = 30LL * 1000LL * 1000LL;
static constexpr uint16_t CONTROL_SUBSCRIBE_MIN_INTERVAL_S = 2;
static constexpr uint16_t CONTROL_SUBSCRIBE_MAX_INTERVAL_S = 30;
static constexpr uint32_t CONTROL_QUEUE_LENGTH = 4;
static constexpr uint32_t CONTROL_TASK_STACK_SIZE = 6144;
static constexpr UBaseType_t CONTROL_TASK_PRIORITY = 5;
static const TickType_t CONTROL_TASK_WAKE_INTERVAL = pdMS_TO_TICKS(5000);

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
    bool command_pending;
    bool pending_command_on;
    bool fallback_poll_active;
    int64_t fallback_last_poll_us;
    char control_state[16];
    char last_error[64];
};

enum class ControlWorkType : uint8_t {
    RelayCommand,
};

struct ControlWork {
    ControlWorkType type;
    bool command_on;
    double temperature_celsius;
    char threshold[8];
    char rule_id[DEVICE_REGISTRY_ID_MAX];
    char actuator_device_id[DEVICE_REGISTRY_ID_MAX];
    char actuator_capability_id[DEVICE_REGISTRY_ID_MAX];
};

struct ControlDecision {
    bool emit_event;
    char event[40];
    bool broadcast_snapshot;
    bool queue_command;
    ControlWork command;
};

struct AttributeTarget {
    uint64_t node_id;
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t attribute_id;
};

static SemaphoreHandle_t mutex = nullptr;
static QueueHandle_t control_queue = nullptr;
static TaskHandle_t control_task_handle = nullptr;
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
    if (latest.relay_known || latest.command_pending || latest.last_command_known) {
        const bool relay_on = latest.relay_known ? latest.relay_on :
            (latest.command_pending ? latest.pending_command_on : latest.last_commanded_on);
        cJSON_AddBoolToObject(item, "relay_on", relay_on);
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

static esp_err_t subscribe_configured_temperature_or_start_fallback(void);

static void set_decision_event(ControlDecision *decision, const char *event, bool broadcast_snapshot = true) {
    if (!decision) return;
    decision->emit_event = true;
    decision->broadcast_snapshot = broadcast_snapshot;
    copy_field(decision->event, event, sizeof(decision->event));
}

static bool effective_relay_state_locked(bool *on) {
    if (!on) return false;
    if (latest.relay_known) {
        *on = latest.relay_on;
        return true;
    }
    if (latest.command_pending) {
        *on = latest.pending_command_on;
        return true;
    }
    if (latest.last_command_known) {
        *on = latest.last_commanded_on;
        return true;
    }
    *on = false;
    return false;
}

static void fill_relay_command_locked(ControlWork *work, bool want_on, const char *threshold) {
    if (!work) return;
    memset(work, 0, sizeof(*work));
    work->type = ControlWorkType::RelayCommand;
    work->command_on = want_on;
    work->temperature_celsius = latest.temperature_celsius;
    copy_field(work->threshold, threshold, sizeof(work->threshold));
    copy_field(work->rule_id, rule.rule_id, sizeof(work->rule_id));
    copy_field(work->actuator_device_id, rule.actuator_device_id, sizeof(work->actuator_device_id));
    copy_field(work->actuator_capability_id, rule.actuator_capability_id, sizeof(work->actuator_capability_id));
}

static cJSON *relay_action_payload(const ControlWork &work) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return nullptr;
    cJSON_AddStringToObject(payload, "rule_id", work.rule_id);
    cJSON_AddNumberToObject(payload, "temperature_celsius", work.temperature_celsius);
    cJSON_AddStringToObject(payload, "threshold", work.threshold);
    cJSON_AddStringToObject(payload, "command", work.command_on ? "on" : "off");
    cJSON_AddItemToObject(payload, "actuator",
                          endpoint_ref_json(work.actuator_device_id, work.actuator_capability_id));
    return payload;
}

static void record_control_command_result(const ControlWork &work, esp_err_t err, const char *error_message) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (latest.command_pending && latest.pending_command_on == work.command_on) {
        latest.command_pending = false;
    }
    if (err == ESP_OK) {
        latest.last_command_known = true;
        latest.last_commanded_on = work.command_on;
        copy_field(latest.control_state, work.command_on ? "cooling" : "idle", sizeof(latest.control_state));
        latest.last_error[0] = '\0';
    } else {
        copy_field(latest.control_state, "error", sizeof(latest.control_state));
        copy_field(latest.last_error, error_message ? error_message : "Control command failed",
                   sizeof(latest.last_error));
    }
    xSemaphoreGive(mutex);

    if (err == ESP_OK) {
        emit_event("control.rule_action", relay_action_payload(work));
    } else {
        emit_event("control.rule_error", nullptr);
    }
    orchestrator_state_broadcast_snapshot();
}

static bool control_work_still_current(const ControlWork &work) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool current = rule.configured && rule.enabled &&
        strncmp(work.rule_id, rule.rule_id, DEVICE_REGISTRY_ID_MAX) == 0 &&
        strncmp(work.actuator_device_id, rule.actuator_device_id, DEVICE_REGISTRY_ID_MAX) == 0 &&
        strncmp(work.actuator_capability_id, rule.actuator_capability_id, DEVICE_REGISTRY_ID_MAX) == 0;
    xSemaphoreGive(mutex);
    return current;
}

static void handle_control_work(const ControlWork &work) {
    if (work.type != ControlWorkType::RelayCommand) return;
    if (!control_work_still_current(work)) {
        ESP_LOGD(TAG, "Dropping stale queued relay command");
        return;
    }

    DeviceRecord actuator = {};
    DeviceCapability capability = {};
    esp_err_t err = find_capability_by_id(work.actuator_device_id, work.actuator_capability_id,
                                          DEVICE_CAPABILITY_RELAY, &actuator, &capability);
    const char *error_message = "Actuator capability not found";
    if (err == ESP_OK) {
        err = execute_cmd_invoke_command(actuator.node_id, capability.endpoint_id, capability.cluster_id,
                                         work.command_on ? 0x01 : 0x00, "{}");
        error_message = "Matter invoke failed";
    }
    record_control_command_result(work, err, error_message);
}

static void run_stale_check(void) {
    bool should_emit = false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (rule.configured && rule.enabled && temp_is_stale_locked() &&
        strcmp(latest.control_state, "stale") != 0) {
        copy_field(latest.control_state, "stale", sizeof(latest.control_state));
        should_emit = true;
    }
    xSemaphoreGive(mutex);

    if (should_emit) {
        emit_event("control.rule_stale", nullptr);
        orchestrator_state_broadcast_snapshot();
        esp_err_t err = subscribe_configured_temperature_or_start_fallback();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Temperature stale resubscribe failed: %s", esp_err_to_name(err));
        }
    }
}

static bool copy_configured_sensor_ids_locked(char *device_id, size_t device_len,
                                              char *capability_id, size_t capability_len) {
    if (!rule.configured || !rule.enabled) return false;
    copy_field(device_id, rule.sensor_device_id, device_len);
    copy_field(capability_id, rule.sensor_capability_id, capability_len);
    return true;
}

static esp_err_t find_configured_temperature_target(AttributeTarget *target) {
    if (!target) return ESP_ERR_INVALID_ARG;

    char sensor_device_id[DEVICE_REGISTRY_ID_MAX] = {};
    char sensor_capability_id[DEVICE_REGISTRY_ID_MAX] = {};
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool has_sensor = copy_configured_sensor_ids_locked(sensor_device_id, sizeof(sensor_device_id),
                                                             sensor_capability_id, sizeof(sensor_capability_id));
    xSemaphoreGive(mutex);
    if (!has_sensor) return ESP_ERR_INVALID_STATE;

    DeviceRecord sensor = {};
    DeviceCapability capability = {};
    ESP_RETURN_ON_ERROR(find_capability_by_id(sensor_device_id, sensor_capability_id,
                                              DEVICE_CAPABILITY_TEMPERATURE, &sensor, &capability),
                        TAG, "configured sensor not found");
    target->node_id = sensor.node_id;
    target->endpoint_id = capability.endpoint_id;
    target->cluster_id = capability.cluster_id;
    target->attribute_id = capability.attribute_id;
    return ESP_OK;
}

static void set_fallback_poll_active(bool active) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    latest.fallback_poll_active = active;
    if (active) latest.fallback_last_poll_us = 0;
    xSemaphoreGive(mutex);
}

static esp_err_t subscribe_temperature_target_or_start_fallback(const AttributeTarget &target) {
    esp_err_t err = execute_attr_subscribe_command(target.node_id, target.endpoint_id, target.cluster_id,
                                                   target.attribute_id,
                                                   CONTROL_SUBSCRIBE_MIN_INTERVAL_S,
                                                   CONTROL_SUBSCRIBE_MAX_INTERVAL_S);
    if (err == ESP_OK) {
        set_fallback_poll_active(false);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Temperature subscription failed (%s); enabling fallback polling", esp_err_to_name(err));
    set_fallback_poll_active(true);
    esp_err_t read_err = execute_attr_read_command(target.node_id, target.endpoint_id,
                                                   target.cluster_id, target.attribute_id);
    if (read_err != ESP_OK) {
        ESP_LOGW(TAG, "Initial fallback temperature read failed: %s", esp_err_to_name(read_err));
    }
    return ESP_OK;
}

static esp_err_t subscribe_configured_temperature_or_start_fallback(void) {
    AttributeTarget target = {};
    ESP_RETURN_ON_ERROR(find_configured_temperature_target(&target), TAG, "configured sensor lookup failed");
    return subscribe_temperature_target_or_start_fallback(target);
}

static void run_fallback_poll_if_due(void) {
    char sensor_device_id[DEVICE_REGISTRY_ID_MAX] = {};
    char sensor_capability_id[DEVICE_REGISTRY_ID_MAX] = {};
    bool should_poll = false;
    const int64_t now = esp_timer_get_time();

    xSemaphoreTake(mutex, portMAX_DELAY);
    if (rule.configured && rule.enabled && latest.fallback_poll_active &&
        (latest.fallback_last_poll_us == 0 ||
         now - latest.fallback_last_poll_us >= FALLBACK_POLL_INTERVAL_US)) {
        latest.fallback_last_poll_us = now;
        copy_field(sensor_device_id, rule.sensor_device_id, sizeof(sensor_device_id));
        copy_field(sensor_capability_id, rule.sensor_capability_id, sizeof(sensor_capability_id));
        should_poll = true;
    }
    xSemaphoreGive(mutex);

    if (!should_poll) return;

    DeviceRecord sensor = {};
    DeviceCapability capability = {};
    esp_err_t err = find_capability_by_id(sensor_device_id, sensor_capability_id,
                                          DEVICE_CAPABILITY_TEMPERATURE, &sensor, &capability);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Fallback polling sensor lookup failed: %s", esp_err_to_name(err));
        return;
    }

    err = execute_attr_read_command(sensor.node_id, capability.endpoint_id,
                                    capability.cluster_id, capability.attribute_id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Fallback temperature read failed: %s", esp_err_to_name(err));
    }
}

static void control_task(void *) {
    while (true) {
        ControlWork work = {};
        if (xQueueReceive(control_queue, &work, CONTROL_TASK_WAKE_INTERVAL) == pdTRUE) {
            handle_control_work(work);
        }
        run_stale_check();
        run_fallback_poll_if_due();
    }
}

static esp_err_t ensure_control_task(void) {
    if (!control_queue) {
        control_queue = xQueueCreate(CONTROL_QUEUE_LENGTH, sizeof(ControlWork));
        if (!control_queue) return ESP_ERR_NO_MEM;
    }
    if (!control_task_handle) {
        if (xTaskCreate(control_task, "temp_control", CONTROL_TASK_STACK_SIZE, nullptr,
                        CONTROL_TASK_PRIORITY, &control_task_handle) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static esp_err_t dispatch_decision(const ControlDecision &decision) {
    if (decision.queue_command) {
        if (!control_queue || xQueueSend(control_queue, &decision.command, 0) != pdTRUE) {
            record_control_command_result(decision.command, ESP_ERR_NO_MEM, "Control action queue full");
            return ESP_ERR_NO_MEM;
        }
        return ESP_OK;
    }

    if (decision.emit_event) {
        emit_event(decision.event, nullptr);
    }
    if (decision.broadcast_snapshot) {
        orchestrator_state_broadcast_snapshot();
    }
    return ESP_OK;
}

static esp_err_t evaluate_locked(bool force_if_unknown, ControlDecision *decision);

static esp_err_t evaluate_and_dispatch(bool force_if_unknown) {
    ControlDecision decision = {};
    xSemaphoreTake(mutex, portMAX_DELAY);
    evaluate_locked(force_if_unknown, &decision);
    xSemaphoreGive(mutex);
    return dispatch_decision(decision);
}

static esp_err_t evaluate_locked(bool force_if_unknown, ControlDecision *decision) {
    if (decision) memset(decision, 0, sizeof(*decision));
    if (!rule.configured || !rule.enabled) {
        copy_field(latest.control_state, "disabled", sizeof(latest.control_state));
        return ESP_OK;
    }
    if (temp_is_stale_locked()) {
        const bool was_stale = strcmp(latest.control_state, "stale") == 0;
        copy_field(latest.control_state, "stale", sizeof(latest.control_state));
        if (!was_stale) {
            set_decision_event(decision, "control.rule_stale");
        }
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
        bool relay_on = false;
        effective_relay_state_locked(&relay_on);
        copy_field(latest.control_state, relay_on ? "cooling" : "idle", sizeof(latest.control_state));
        set_decision_event(decision, "control.rule_evaluated");
        return ESP_OK;
    }

    bool current_on = false;
    const bool known_state = effective_relay_state_locked(&current_on);
    if (known_state && current_on == want_on && !force_if_unknown) {
        copy_field(latest.control_state, want_on ? "cooling" : "idle", sizeof(latest.control_state));
        set_decision_event(decision, "control.rule_evaluated");
        return ESP_OK;
    }

    latest.command_pending = true;
    latest.pending_command_on = want_on;
    if (decision) {
        decision->queue_command = true;
        fill_relay_command_locked(&decision->command, want_on, threshold);
    }
    return ESP_OK;
}

esp_err_t temperature_control_init(void) {
    if (!mutex) {
        mutex = xSemaphoreCreateMutex();
        if (!mutex) return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(ensure_control_task(), TAG, "control task init failed");
    xSemaphoreTake(mutex, portMAX_DELAY);
    memset(&latest, 0, sizeof(latest));
    esp_err_t err = load_rule_locked();
    copy_field(latest.control_state, rule.enabled ? "idle" : "disabled", sizeof(latest.control_state));
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
    latest.command_pending = false;
    latest.fallback_poll_active = false;
    latest.fallback_last_poll_us = 0;
    latest.last_error[0] = '\0';
    esp_err_t err = save_rule_locked();
    xSemaphoreGive(mutex);
    if (err == ESP_OK && enabled) {
        AttributeTarget target = {};
        target.node_id = sensor.node_id;
        target.endpoint_id = sensor_cap.endpoint_id;
        target.cluster_id = sensor_cap.cluster_id;
        target.attribute_id = sensor_cap.attribute_id;
        subscribe_temperature_target_or_start_fallback(target);
        evaluate_and_dispatch(true);
    }
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

esp_err_t temperature_control_get_chamber(cJSON **out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    *out = chamber_json_locked();
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
    if (!enabled) {
        latest.command_pending = false;
        latest.fallback_poll_active = false;
        latest.fallback_last_poll_us = 0;
    }
    esp_err_t err = save_rule_locked();
    xSemaphoreGive(mutex);
    if (err == ESP_OK && enabled) {
        esp_err_t subscribe_err = subscribe_configured_temperature_or_start_fallback();
        if (subscribe_err != ESP_OK) {
            xSemaphoreTake(mutex, portMAX_DELAY);
            copy_field(latest.control_state, "error", sizeof(latest.control_state));
            copy_field(latest.last_error, "Sensor capability not found", sizeof(latest.last_error));
            xSemaphoreGive(mutex);
            emit_event("control.rule_error", nullptr);
            err = subscribe_err;
        } else {
            evaluate_and_dispatch(true);
        }
    }
    emit_event(enabled ? "control.rule_enabled" : "control.rule_disabled", nullptr);
    orchestrator_state_broadcast_snapshot();
    return err;
}

esp_err_t temperature_control_note_manual_relay_command(const char *device_id,
                                                        const char *capability_id,
                                                        bool on) {
    if (!device_id || !capability_id) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (rule.configured &&
        strncmp(device_id, rule.actuator_device_id, DEVICE_REGISTRY_ID_MAX) == 0 &&
        strncmp(capability_id, rule.actuator_capability_id, DEVICE_REGISTRY_ID_MAX) == 0) {
        latest.last_command_known = true;
        latest.last_commanded_on = on;
        latest.command_pending = false;
        copy_field(latest.control_state, rule.enabled ? (on ? "cooling" : "idle") : "disabled",
                   sizeof(latest.control_state));
    }
    xSemaphoreGive(mutex);
    orchestrator_state_broadcast_snapshot();
    return ESP_OK;
}

esp_err_t temperature_control_delete_rule(void) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    init_rule(&rule);
    copy_field(latest.control_state, "disabled", sizeof(latest.control_state));
    latest.command_pending = false;
    latest.fallback_poll_active = false;
    latest.fallback_last_poll_us = 0;
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
    ControlDecision decision = {};
    bool evaluated_rule = false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (capability.semantic_type == DEVICE_CAPABILITY_TEMPERATURE) {
        latest.has_temperature = true;
        latest.raw_temperature = raw_value;
        latest.temperature_celsius = raw_value / 100.0;
        latest.temperature_time_us = esp_timer_get_time();
        if (rule.configured &&
            strncmp(record.device_id, rule.sensor_device_id, DEVICE_REGISTRY_ID_MAX) == 0 &&
            strncmp(capability.capability_id, rule.sensor_capability_id, DEVICE_REGISTRY_ID_MAX) == 0) {
            evaluate_locked(false, &decision);
            evaluated_rule = true;
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
    if (evaluated_rule) {
        esp_err_t err = dispatch_decision(decision);
        if (!decision.queue_command && !decision.broadcast_snapshot) {
            orchestrator_state_broadcast_snapshot();
        }
        return err;
    }
    orchestrator_state_broadcast_snapshot();
    return ESP_OK;
}

esp_err_t temperature_control_resume_after_matter_controller_init(void) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool should_resume = rule.configured && rule.enabled;
    xSemaphoreGive(mutex);
    if (!should_resume) return ESP_OK;

    esp_err_t err = subscribe_configured_temperature_or_start_fallback();
    if (err != ESP_OK) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        copy_field(latest.control_state, "error", sizeof(latest.control_state));
        copy_field(latest.last_error, "Sensor capability not found", sizeof(latest.last_error));
        xSemaphoreGive(mutex);
        emit_event("control.rule_error", nullptr);
        orchestrator_state_broadcast_snapshot();
        return err;
    }
    evaluate_and_dispatch(true);
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
