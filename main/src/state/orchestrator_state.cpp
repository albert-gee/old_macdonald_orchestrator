#include "state/orchestrator_state.h"

#include "control/temperature_control.h"
#include "registry/device_registry.h"
#include "websocket_server.h"

#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct WifiState {
    bool ap_running;
    bool sta_configured;
    bool sta_connected;
    char sta_ip[16];
    int rssi;
};

struct ThreadState {
    bool enabled;
    bool attached;
    char role[16];
    bool dataset_present;
};

struct MatterState {
    bool platform_initialized;
    char platform_error[96];
    bool controller_initialized;
    size_t commissioned_node_count;
};

struct WebsocketState {
    size_t client_count;
};

struct OrchestratorState {
    WifiState wifi;
    ThreadState thread;
    MatterState matter;
    WebsocketState websocket;
};

static OrchestratorState state = {};
static SemaphoreHandle_t state_mutex = nullptr;

static void copy_string(char *dest, const char *src, size_t len) {
    if (!dest || len == 0) return;
    if (!src) src = "";
    strncpy(dest, src, len - 1);
    dest[len - 1] = '\0';
}

static esp_err_t with_lock(void (*fn)(void *), void *ctx) {
    if (!state_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    fn(ctx);
    xSemaphoreGive(state_mutex);
    return ESP_OK;
}

esp_err_t orchestrator_state_init(void) {
    if (!state_mutex) {
        state_mutex = xSemaphoreCreateMutex();
        if (!state_mutex) return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    memset(&state, 0, sizeof(state));
    copy_string(state.thread.role, "disabled", sizeof(state.thread.role));
    state.wifi.rssi = 0;
    xSemaphoreGive(state_mutex);
    return ESP_OK;
}

esp_err_t orchestrator_state_set_wifi_ap_running(bool running) {
    return with_lock([](void *ctx) { state.wifi.ap_running = *static_cast<bool *>(ctx); }, &running);
}

esp_err_t orchestrator_state_set_wifi_sta_configured(bool configured) {
    return with_lock([](void *ctx) { state.wifi.sta_configured = *static_cast<bool *>(ctx); }, &configured);
}

esp_err_t orchestrator_state_set_wifi_sta_connected(bool connected, const char *ip) {
    struct Ctx { bool connected; const char *ip; } ctx = {connected, ip};
    return with_lock([](void *raw) {
        auto *ctx = static_cast<Ctx *>(raw);
        state.wifi.sta_connected = ctx->connected;
        copy_string(state.wifi.sta_ip, ctx->connected ? ctx->ip : "", sizeof(state.wifi.sta_ip));
    }, &ctx);
}

esp_err_t orchestrator_state_set_wifi_rssi(int rssi) {
    return with_lock([](void *ctx) { state.wifi.rssi = *static_cast<int *>(ctx); }, &rssi);
}

esp_err_t orchestrator_state_set_thread_enabled(bool enabled) {
    return with_lock([](void *ctx) { state.thread.enabled = *static_cast<bool *>(ctx); }, &enabled);
}

esp_err_t orchestrator_state_set_thread_attached(bool attached) {
    return with_lock([](void *ctx) { state.thread.attached = *static_cast<bool *>(ctx); }, &attached);
}

esp_err_t orchestrator_state_set_thread_role(const char *role) {
    return with_lock([](void *ctx) {
        copy_string(state.thread.role, static_cast<const char *>(ctx), sizeof(state.thread.role));
    }, const_cast<char *>(role));
}

esp_err_t orchestrator_state_set_thread_dataset_present(bool present) {
    return with_lock([](void *ctx) { state.thread.dataset_present = *static_cast<bool *>(ctx); }, &present);
}

esp_err_t orchestrator_state_set_matter_platform_initialized(bool initialized) {
    return with_lock([](void *ctx) {
        state.matter.platform_initialized = *static_cast<bool *>(ctx);
        if (state.matter.platform_initialized) {
            state.matter.platform_error[0] = '\0';
        }
    }, &initialized);
}

esp_err_t orchestrator_state_set_matter_platform_error(const char *error) {
    return with_lock([](void *ctx) {
        state.matter.platform_initialized = false;
        state.matter.controller_initialized = false;
        copy_string(state.matter.platform_error, static_cast<const char *>(ctx), sizeof(state.matter.platform_error));
    }, const_cast<char *>(error));
}

esp_err_t orchestrator_state_get_matter_platform_status(bool *initialized, char *error, size_t error_len) {
    if (!initialized) return ESP_ERR_INVALID_ARG;
    if (!state_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    *initialized = state.matter.platform_initialized;
    if (error && error_len > 0) {
        copy_string(error, state.matter.platform_error, error_len);
    }
    xSemaphoreGive(state_mutex);
    return ESP_OK;
}

esp_err_t orchestrator_state_set_matter_controller_initialized(bool initialized) {
    return with_lock([](void *ctx) { state.matter.controller_initialized = *static_cast<bool *>(ctx); }, &initialized);
}

esp_err_t orchestrator_state_set_matter_commissioned_node_count(size_t count) {
    return with_lock([](void *ctx) { state.matter.commissioned_node_count = *static_cast<size_t *>(ctx); }, &count);
}

esp_err_t orchestrator_state_set_websocket_client_count(size_t count) {
    return with_lock([](void *ctx) { state.websocket.client_count = *static_cast<size_t *>(ctx); }, &count);
}

static cJSON *snapshot_to_json(const OrchestratorState &snapshot) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return nullptr;

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi, "mode", "apsta");
    cJSON_AddBoolToObject(wifi, "ap_running", snapshot.wifi.ap_running);
    cJSON_AddBoolToObject(wifi, "sta_configured", snapshot.wifi.sta_configured);
    cJSON_AddBoolToObject(wifi, "sta_connected", snapshot.wifi.sta_connected);
    if (snapshot.wifi.sta_connected && snapshot.wifi.sta_ip[0]) {
        cJSON_AddStringToObject(wifi, "sta_ip", snapshot.wifi.sta_ip);
    } else {
        cJSON_AddNullToObject(wifi, "sta_ip");
    }
    if (snapshot.wifi.sta_connected) {
        cJSON_AddNumberToObject(wifi, "rssi", snapshot.wifi.rssi);
    } else {
        cJSON_AddNullToObject(wifi, "rssi");
    }

    cJSON *thread = cJSON_AddObjectToObject(root, "thread");
    cJSON_AddBoolToObject(thread, "enabled", snapshot.thread.enabled);
    cJSON_AddBoolToObject(thread, "attached", snapshot.thread.attached);
    cJSON_AddStringToObject(thread, "role", snapshot.thread.role[0] ? snapshot.thread.role : "disabled");
    cJSON_AddBoolToObject(thread, "dataset_present", snapshot.thread.dataset_present);

    cJSON *matter = cJSON_AddObjectToObject(root, "matter");
    cJSON_AddBoolToObject(matter, "platform_initialized", snapshot.matter.platform_initialized);
    if (snapshot.matter.platform_error[0]) {
        cJSON_AddStringToObject(matter, "platform_error", snapshot.matter.platform_error);
    } else {
        cJSON_AddNullToObject(matter, "platform_error");
    }
    cJSON_AddBoolToObject(matter, "controller_initialized", snapshot.matter.controller_initialized);
    cJSON *nodes = device_registry_commissioned_nodes_to_json();
    if (nodes) cJSON_AddItemToObject(matter, "commissioned_nodes", nodes);
    else cJSON_AddItemToObject(matter, "commissioned_nodes", cJSON_CreateArray());

    cJSON *websocket = cJSON_AddObjectToObject(root, "websocket");
    cJSON_AddNumberToObject(websocket, "clients", snapshot.websocket.client_count);

    cJSON *registry = device_registry_to_json();
    cJSON *devices = registry ? cJSON_DetachItemFromObject(registry, "devices") : nullptr;
    if (devices) cJSON_AddItemToObject(root, "devices", devices);
    else cJSON_AddItemToObject(root, "devices", cJSON_CreateArray());
    cJSON_Delete(registry);

    temperature_control_add_snapshot_fields(root);

    return root;
}

cJSON *orchestrator_state_to_json(void) {
    if (!state_mutex) return nullptr;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    OrchestratorState snapshot = state;
    xSemaphoreGive(state_mutex);
    return snapshot_to_json(snapshot);
}

static esp_err_t send_snapshot(int client_fd) {
    if (client_fd < 0 && !websocket_server_is_running()) {
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *payload = orchestrator_state_to_json();
    if (!root || !payload) {
        cJSON_Delete(root);
        cJSON_Delete(payload);
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(root, "type", "state_snapshot");
    cJSON_AddItemToObject(root, "payload", payload);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_FAIL;

    esp_err_t err = client_fd >= 0 ? websocket_send_message_to_client(client_fd, json)
                                   : websocket_broadcast_message(json);
    free(json);
    return err;
}

esp_err_t orchestrator_state_broadcast_snapshot(void) {
    return send_snapshot(-1);
}

esp_err_t orchestrator_state_send_snapshot_to_client(int client_fd) {
    return send_snapshot(client_fd);
}

esp_err_t orchestrator_state_broadcast_event(const char *event, cJSON *payload) {
    if (!event) {
        cJSON_Delete(payload);
        return ESP_ERR_INVALID_ARG;
    }

    if (!websocket_server_is_running()) {
        cJSON_Delete(payload);
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(payload);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "event");
    cJSON_AddStringToObject(root, "event", event);
    if (payload) {
        cJSON_AddItemToObject(root, "payload", payload);
    } else {
        cJSON *empty = cJSON_CreateObject();
        if (!empty) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddItemToObject(root, "payload", empty);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_FAIL;

    esp_err_t err = websocket_broadcast_message(json);
    free(json);
    return err;
}
