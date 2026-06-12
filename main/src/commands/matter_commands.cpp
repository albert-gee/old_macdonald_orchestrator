#include "commands/matter_commands.h"

#include "control/temperature_control.h"
#include "matter_interface.h"
#include "matter_controller.h"
#include "messages/outbound_message_builder.h"
#include "registry/device_registry.h"
#include "state/orchestrator_state.h"
#include "storage/nvs_diagnostics.h"
#include "thread_util.h"
#include "websocket_server.h"

#include <cJSON.h>
#include <esp_matter_controller_pairing_command.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <optional>

#include "event_handlers/chip_event_handler.h"

static constexpr const char *TAG = "MATTER_COMMANDS";
static constexpr uint32_t MATTER_COMMAND_QUEUE_LENGTH = 4;
static constexpr uint32_t MATTER_COMMAND_TASK_STACK_SIZE = 8192;
static constexpr UBaseType_t MATTER_COMMAND_TASK_PRIORITY = 5;
static constexpr size_t MATTER_COMMAND_REQUEST_ID_LENGTH = 64;
static constexpr size_t MATTER_COMMAND_ACTION_LENGTH = 64;

struct MatterCommandWork {
    int client_fd;
    char request_id[MATTER_COMMAND_REQUEST_ID_LENGTH];
    char action[MATTER_COMMAND_ACTION_LENGTH];
    uint64_t node_id;
    uint64_t fabric_id;
    uint16_t listen_port;
};

static QueueHandle_t matter_command_queue = nullptr;
static TaskHandle_t matter_command_task = nullptr;
static portMUX_TYPE pending_commissioning_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t pending_commissioning_node_id = 0;

static void set_pending_commissioning_node(uint64_t node_id) {
    portENTER_CRITICAL(&pending_commissioning_lock);
    pending_commissioning_node_id = node_id;
    portEXIT_CRITICAL(&pending_commissioning_lock);
}

static uint64_t take_pending_commissioning_node(uint64_t fallback_node_id) {
    portENTER_CRITICAL(&pending_commissioning_lock);
    const uint64_t node_id = pending_commissioning_node_id ? pending_commissioning_node_id : fallback_node_id;
    pending_commissioning_node_id = 0;
    portEXIT_CRITICAL(&pending_commissioning_lock);
    return node_id;
}

static chip::FabricIndex normalized_fabric_index(chip::ScopedNodeId peer_id) {
    const uint64_t maybe_fabric = peer_id.GetNodeId();
    if (maybe_fabric > 0 && maybe_fabric <= UINT8_MAX) {
        return static_cast<chip::FabricIndex>(maybe_fabric);
    }
    return peer_id.GetFabricIndex();
}

static void upsert_commissioned_node(uint64_t node_id, chip::FabricIndex fabric_index) {
    DeviceRecord record = {};
    const esp_err_t existing = device_registry_get_device_by_node_id(node_id, &record);
    if (existing != ESP_OK) {
        snprintf(record.device_id, sizeof(record.device_id), "node-%" PRIu64, node_id);
        snprintf(record.label, sizeof(record.label), "Matter node %" PRIu64, node_id);
        record.node_id = node_id;
    }
    record.reachable = true;
    if (!record.device_id[0]) {
        snprintf(record.device_id, sizeof(record.device_id), "node-%" PRIu64, node_id);
    }
    if (!record.label[0]) {
        snprintf(record.label, sizeof(record.label), "Matter node %" PRIu64, node_id);
    }

    const esp_err_t err = device_registry_upsert_device(&record);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to upsert commissioned node 0x%" PRIX64 ": %s",
                 node_id, esp_err_to_name(err));
        return;
    }

    cJSON *payload = cJSON_CreateObject();
    if (payload) {
        char node_id_text[24] = {};
        snprintf(node_id_text, sizeof(node_id_text), "%" PRIu64, node_id);
        cJSON_AddNumberToObject(payload, "node_id", static_cast<double>(node_id));
        cJSON_AddStringToObject(payload, "node_id_text", node_id_text);
        cJSON_AddStringToObject(payload, "device_id", record.device_id);
        cJSON_AddNumberToObject(payload, "fabric_index", fabric_index);
        orchestrator_state_broadcast_event("matter.commissioning_complete", payload);
    }

    broadcast_info_matter_commissioning_complete_message(node_id, fabric_index);
    orchestrator_state_set_matter_last_error(nullptr);
}

static void pairing_pase_callback(CHIP_ERROR err) {
    if (err == CHIP_NO_ERROR) {
        ESP_LOGI(TAG, "Matter PASE session established");
    } else {
        ESP_LOGE(TAG, "Matter PASE session failed: %s", err.AsString());
        orchestrator_state_set_matter_last_error(err.AsString());
    }
}

static void pairing_success_callback(chip::ScopedNodeId peer_id) {
    matter_controller_clear_wifi_operational_address_hint();
    const uint64_t node_id = take_pending_commissioning_node(peer_id.GetNodeId());
    const chip::FabricIndex fabric_index = normalized_fabric_index(peer_id);
    ESP_LOGI(TAG, "Matter commissioning success callback: node=0x%" PRIX64 " fabric=%u",
             node_id, static_cast<unsigned>(fabric_index));
    upsert_commissioned_node(node_id, fabric_index);
}

static void pairing_failure_callback(
    chip::ScopedNodeId peer_id,
    CHIP_ERROR error,
    chip::Controller::CommissioningStage stage,
    std::optional<chip::Credentials::AttestationVerificationResult>) {
    matter_controller_clear_wifi_operational_address_hint();
    const uint64_t node_id = take_pending_commissioning_node(peer_id.GetNodeId());
    const chip::FabricIndex fabric_index = normalized_fabric_index(peer_id);
    ESP_LOGE(TAG, "Matter commissioning failed: node=0x%" PRIX64 " fabric=%u stage=%u error=%s",
             node_id,
             static_cast<unsigned>(fabric_index),
             static_cast<unsigned>(stage),
             error.AsString());
    orchestrator_state_set_matter_last_error(error.AsString());

    cJSON *payload = cJSON_CreateObject();
    if (payload) {
        char node_id_text[24] = {};
        snprintf(node_id_text, sizeof(node_id_text), "%" PRIu64, node_id);
        cJSON_AddNumberToObject(payload, "node_id", static_cast<double>(node_id));
        cJSON_AddStringToObject(payload, "node_id_text", node_id_text);
        cJSON_AddNumberToObject(payload, "fabric_index", fabric_index);
        cJSON_AddNumberToObject(payload, "stage", static_cast<int>(stage));
        cJSON_AddStringToObject(payload, "error", error.AsString());
        orchestrator_state_broadcast_event("matter.commissioning_failed", payload);
    }
    orchestrator_state_broadcast_snapshot();
}

static void install_pairing_callbacks() {
    esp_matter::controller::pairing_command_callbacks_t callbacks = {
        .pase_callback = pairing_pase_callback,
        .commissioning_success_callback = pairing_success_callback,
        .commissioning_failure_callback = pairing_failure_callback,
    };
    esp_matter::controller::pairing_command::get_instance().set_callbacks(callbacks);
}

static void copy_string(char *dest, const char *src, size_t len) {
    if (!dest || len == 0) return;
    if (!src) src = "";
    strncpy(dest, src, len - 1);
    dest[len - 1] = '\0';
}

static esp_err_t send_json_to_client(int client_fd, cJSON *root) {
    char *json = cJSON_PrintUnformatted(root);
    if (!json) return ESP_FAIL;
    esp_err_t err = websocket_send_message_to_client(client_fd, json);
    free(json);
    return err;
}

static esp_err_t send_matter_success(int client_fd,
                                     const char *request_id,
                                     const char *action,
                                     cJSON *payload) {
    cJSON *root = cJSON_CreateObject();
    if (!root || !payload) {
        cJSON_Delete(root);
        cJSON_Delete(payload);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "command_result");
    cJSON_AddStringToObject(root, "request_id", request_id);
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddItemToObject(root, "payload", payload);
    esp_err_t err = send_json_to_client(client_fd, root);
    cJSON_Delete(root);
    return err;
}

static esp_err_t send_matter_error(int client_fd,
                                   const char *request_id,
                                   const char *action,
                                   const char *code,
                                   const char *message,
                                   esp_err_t command_err,
                                   const MatterCommandWork &work) {
    cJSON *root = cJSON_CreateObject();
    cJSON *error = cJSON_CreateObject();
    cJSON *details = cJSON_CreateObject();
    if (!root || !error || !details) {
        cJSON_Delete(root);
        cJSON_Delete(error);
        cJSON_Delete(details);
        return ESP_ERR_NO_MEM;
    }

    char node_id[24] = {};
    snprintf(node_id, sizeof(node_id), "%" PRIu64, work.node_id);

    cJSON_AddStringToObject(root, "type", "command_result");
    cJSON_AddStringToObject(root, "request_id", request_id);
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    cJSON_AddStringToObject(details, "esp_err", esp_err_to_name(command_err));
    cJSON_AddStringToObject(details, "node_id", node_id);
    cJSON_AddNumberToObject(details, "fabric_id", static_cast<double>(work.fabric_id));
    cJSON_AddNumberToObject(details, "listen_port", work.listen_port);
    cJSON *storage = nvs_diagnostics_to_json();
    if (storage) cJSON_AddItemToObject(details, "storage", storage);
    cJSON_AddItemToObject(error, "details", details);
    cJSON_AddItemToObject(root, "error", error);
    esp_err_t err = send_json_to_client(client_fd, root);
    cJSON_Delete(root);
    return err;
}

static cJSON *matter_controller_init_payload(const MatterCommandWork &work) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return nullptr;
    char node_id[24] = {};
    snprintf(node_id, sizeof(node_id), "%" PRIu64, work.node_id);
    cJSON_AddBoolToObject(payload, "controller_initialized", true);
    cJSON_AddStringToObject(payload, "controller_node_id", node_id);
    cJSON_AddNumberToObject(payload, "fabric_id", static_cast<double>(work.fabric_id));
    cJSON_AddNumberToObject(payload, "listen_port", work.listen_port);
    return payload;
}

static void matter_command_worker(void *context) {
    MatterCommandWork work = {};
    while (xQueueReceive(matter_command_queue, &work, portMAX_DELAY) == pdTRUE) {
        if (strcmp(work.action, "matter.controller_init") != 0) {
            memset(&work, 0, sizeof(work));
            continue;
        }

        ESP_LOGI(TAG, "Executing Matter controller init in worker: node=%" PRIu64
                 " fabric=%" PRIu64 " port=%u",
                 work.node_id, work.fabric_id, static_cast<unsigned>(work.listen_port));
        esp_err_t err = execute_matter_controller_init_command(work.node_id, work.fabric_id, work.listen_port);
        if (err == ESP_OK) {
            install_pairing_callbacks();
            send_matter_success(work.client_fd,
                                work.request_id,
                                work.action,
                                matter_controller_init_payload(work));
        } else {
            char last_error[192] = {};
            snprintf(last_error, sizeof(last_error), "MATTER_CONTROLLER_INIT_FAILED:%s", esp_err_to_name(err));
            orchestrator_state_set_matter_controller_initialized(false);
            orchestrator_state_set_matter_last_error(last_error);
            send_matter_error(work.client_fd,
                              work.request_id,
                              work.action,
                              "MATTER_CONTROLLER_INIT_FAILED",
                              "Matter controller initialization failed.",
                              err,
                              work);
            orchestrator_state_broadcast_snapshot();
        }
        memset(&work, 0, sizeof(work));
    }
}

esp_err_t matter_command_service_init(void) {
    if (!matter_command_queue) {
        matter_command_queue = xQueueCreate(MATTER_COMMAND_QUEUE_LENGTH, sizeof(MatterCommandWork));
        if (!matter_command_queue) return ESP_ERR_NO_MEM;
    }
    if (!matter_command_task) {
        BaseType_t ok = xTaskCreate(matter_command_worker,
                                    "matter_cmd",
                                    MATTER_COMMAND_TASK_STACK_SIZE,
                                    nullptr,
                                    MATTER_COMMAND_TASK_PRIORITY,
                                    &matter_command_task);
        if (ok != pdPASS) return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool matter_command_is_deferred_action(const char *action) {
    return action && strcmp(action, "matter.controller_init") == 0;
}

esp_err_t matter_command_enqueue_controller_init(int client_fd,
                                                 const char *request_id,
                                                 uint64_t node_id,
                                                 uint64_t fabric_id,
                                                 uint16_t listen_port,
                                                 char *error_code,
                                                 size_t error_code_len,
                                                 char *error_message,
                                                 size_t error_message_len) {
    if (!request_id) {
        copy_string(error_code, "INVALID_MATTER_COMMAND", error_code_len);
        copy_string(error_message, "Invalid Matter command request.", error_message_len);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t init_err = matter_command_service_init();
    if (init_err != ESP_OK) {
        copy_string(error_code, "MATTER_COMMAND_SERVICE_UNAVAILABLE", error_code_len);
        copy_string(error_message, "Matter command worker is unavailable.", error_message_len);
        return init_err;
    }

    MatterCommandWork work = {};
    work.client_fd = client_fd;
    copy_string(work.request_id, request_id, sizeof(work.request_id));
    copy_string(work.action, "matter.controller_init", sizeof(work.action));
    work.node_id = node_id;
    work.fabric_id = fabric_id;
    work.listen_port = listen_port;

    if (xQueueSend(matter_command_queue, &work, pdMS_TO_TICKS(100)) != pdTRUE) {
        copy_string(error_code, "MATTER_COMMAND_BUSY", error_code_len);
        copy_string(error_message, "Matter command worker queue is full.", error_message_len);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t execute_matter_pair_ble_thread_command(const uint64_t node_id,
                                                 const uint32_t pin,
                                                 const uint16_t discriminator,
                                                 const char *operational_ip,
                                                 const uint16_t operational_port) {
    uint8_t dataset_len = OT_OPERATIONAL_DATASET_MAX_LENGTH;
    auto *tlvs = static_cast<uint8_t *>(malloc(dataset_len));
    if (!tlvs) return ESP_ERR_NO_MEM;

    esp_err_t err = thread_get_active_dataset_tlvs(tlvs, &dataset_len);
    if (err != ESP_OK) {
        free(tlvs);
        return err;
    }

    install_pairing_callbacks();
    set_pending_commissioning_node(node_id);
    if (operational_ip && operational_ip[0]) {
        matter_controller_prepare_operational_address_hint(node_id, operational_ip, operational_port);
    } else {
        matter_controller_prepare_thread_operational_address_hint(node_id, operational_port);
    }
    // pairing_ble_thread copies the dataset into the Matter command path; this caller owns tlvs.
    err = pairing_ble_thread(node_id, pin, discriminator, tlvs, dataset_len);
    free(tlvs);

    return err;
}

esp_err_t execute_matter_pair_ble_wifi_command(const uint64_t node_id,
                                               const uint32_t pin,
                                               const uint16_t discriminator,
                                               const char *ssid,
                                               const char *password) {
    if (!ssid || !ssid[0] || !password) return ESP_ERR_INVALID_ARG;
    install_pairing_callbacks();
    set_pending_commissioning_node(node_id);
    matter_controller_prepare_wifi_operational_address_hint(node_id, 5540);
    return pairing_ble_wifi(node_id, pin, discriminator, ssid, password);
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
    esp_err_t err = matter_controller_init(node_id, fabric_id, listen_port, attribute_data_report_callback, subscribe_done_callback);
    if (err == ESP_OK) {
        orchestrator_state_set_matter_controller_initialized(true);
        orchestrator_state_set_matter_controller_config(node_id, fabric_id, listen_port);
        orchestrator_state_set_matter_last_error(nullptr);
        esp_err_t resume_err = temperature_control_resume_after_matter_controller_init();
        if (resume_err != ESP_OK) {
            ESP_LOGW(TAG, "Temperature control resume failed: %s", esp_err_to_name(resume_err));
        }
        orchestrator_state_broadcast_event("matter.controller_initialized", nullptr);
        orchestrator_state_broadcast_snapshot();
    }
    return err;
}
