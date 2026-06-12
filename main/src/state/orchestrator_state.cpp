#include "state/orchestrator_state.h"

#include "control/temperature_control.h"
#include "registry/device_registry.h"
#include "storage/nvs_diagnostics.h"
#include "websocket_server.h"

#include <cstring>
#include <cstdio>
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
    bool platform_initialized;
    bool enabled;
    bool interface_up;
    bool attached;
    char role[16];
    bool dataset_present;
    char network_name[32];
    int channel;
    int pan_id;
    char extended_pan_id[24];
    char mesh_local_prefix[48];
    char active_timestamp[32];
    char security_policy[64];
    char rloc16[16];
    char ext_address[24];
    char eui64[24];
    bool leader_data_present;
    int leader_partition_id;
    int leader_weighting;
    int leader_data_version;
    int leader_stable_data_version;
    int leader_router_id;
    int unicast_address_count;
    int multicast_address_count;
    int router_count;
    int child_count;
    int neighbor_count;
    bool border_router_initialized;
    char last_cli_error[192];
};

struct MatterState {
    bool platform_initialized;
    char platform_error[192];
    bool controller_initialized;
    char controller_node_id[24];
    uint64_t fabric_id;
    uint16_t listen_port;
    char last_error[192];
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
    state.thread.channel = -1;
    state.thread.pan_id = -1;
    state.thread.unicast_address_count = -1;
    state.thread.multicast_address_count = -1;
    state.thread.router_count = -1;
    state.thread.child_count = -1;
    state.thread.neighbor_count = -1;
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

esp_err_t orchestrator_state_set_thread_platform_initialized(bool initialized) {
    return with_lock([](void *ctx) { state.thread.platform_initialized = *static_cast<bool *>(ctx); }, &initialized);
}

esp_err_t orchestrator_state_set_thread_interface_up(bool interface_up) {
    return with_lock([](void *ctx) { state.thread.interface_up = *static_cast<bool *>(ctx); }, &interface_up);
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
    return with_lock([](void *ctx) {
        state.thread.dataset_present = *static_cast<bool *>(ctx);
        if (!state.thread.dataset_present) {
            state.thread.network_name[0] = '\0';
            state.thread.channel = -1;
            state.thread.pan_id = -1;
            state.thread.extended_pan_id[0] = '\0';
            state.thread.mesh_local_prefix[0] = '\0';
            state.thread.active_timestamp[0] = '\0';
            state.thread.security_policy[0] = '\0';
        }
    }, &present);
}

esp_err_t orchestrator_state_set_thread_dataset_fields(const char *network_name,
                                                       int channel,
                                                       int pan_id,
                                                       const char *extended_pan_id,
                                                       const char *mesh_local_prefix,
                                                       const char *active_timestamp,
                                                       const char *security_policy) {
    struct Ctx {
        const char *network_name;
        int channel;
        int pan_id;
        const char *extended_pan_id;
        const char *mesh_local_prefix;
        const char *active_timestamp;
        const char *security_policy;
    } ctx = {network_name, channel, pan_id, extended_pan_id, mesh_local_prefix, active_timestamp, security_policy};
    return with_lock([](void *raw) {
        auto *ctx = static_cast<Ctx *>(raw);
        if (ctx->network_name) copy_string(state.thread.network_name, ctx->network_name, sizeof(state.thread.network_name));
        if (ctx->channel >= 0) state.thread.channel = ctx->channel;
        if (ctx->pan_id >= 0) state.thread.pan_id = ctx->pan_id;
        if (ctx->extended_pan_id) copy_string(state.thread.extended_pan_id, ctx->extended_pan_id, sizeof(state.thread.extended_pan_id));
        if (ctx->mesh_local_prefix) copy_string(state.thread.mesh_local_prefix, ctx->mesh_local_prefix, sizeof(state.thread.mesh_local_prefix));
        if (ctx->active_timestamp) copy_string(state.thread.active_timestamp, ctx->active_timestamp, sizeof(state.thread.active_timestamp));
        if (ctx->security_policy) copy_string(state.thread.security_policy, ctx->security_policy, sizeof(state.thread.security_policy));
    }, &ctx);
}

esp_err_t orchestrator_state_set_thread_rloc16(const char *rloc16) {
    return with_lock([](void *ctx) {
        copy_string(state.thread.rloc16, static_cast<const char *>(ctx), sizeof(state.thread.rloc16));
    }, const_cast<char *>(rloc16));
}

esp_err_t orchestrator_state_set_thread_ext_address(const char *ext_address) {
    return with_lock([](void *ctx) {
        copy_string(state.thread.ext_address, static_cast<const char *>(ctx), sizeof(state.thread.ext_address));
    }, const_cast<char *>(ext_address));
}

esp_err_t orchestrator_state_set_thread_eui64(const char *eui64) {
    return with_lock([](void *ctx) {
        copy_string(state.thread.eui64, static_cast<const char *>(ctx), sizeof(state.thread.eui64));
    }, const_cast<char *>(eui64));
}

static int json_int_field(const cJSON *object, const char *name, int fallback) {
    const cJSON *item = cJSON_GetObjectItem(object, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

esp_err_t orchestrator_state_set_thread_leader_data(const cJSON *leader_data) {
    if (!leader_data) return ESP_ERR_INVALID_ARG;
    return with_lock([](void *ctx) {
        const cJSON *leader = static_cast<const cJSON *>(ctx);
        state.thread.leader_data_present = true;
        state.thread.leader_partition_id = json_int_field(leader, "partition_id", state.thread.leader_partition_id);
        state.thread.leader_weighting = json_int_field(leader, "weighting", state.thread.leader_weighting);
        state.thread.leader_data_version = json_int_field(leader, "data_version", state.thread.leader_data_version);
        state.thread.leader_stable_data_version = json_int_field(leader, "stable_data_version", state.thread.leader_stable_data_version);
        state.thread.leader_router_id = json_int_field(leader, "leader_router_id", state.thread.leader_router_id);
    }, const_cast<cJSON *>(leader_data));
}

esp_err_t orchestrator_state_set_thread_unicast_address_count(int count) {
    return with_lock([](void *ctx) { state.thread.unicast_address_count = *static_cast<int *>(ctx); }, &count);
}

esp_err_t orchestrator_state_set_thread_multicast_address_count(int count) {
    return with_lock([](void *ctx) { state.thread.multicast_address_count = *static_cast<int *>(ctx); }, &count);
}

esp_err_t orchestrator_state_set_thread_router_count(int count) {
    return with_lock([](void *ctx) { state.thread.router_count = *static_cast<int *>(ctx); }, &count);
}

esp_err_t orchestrator_state_set_thread_child_count(int count) {
    return with_lock([](void *ctx) { state.thread.child_count = *static_cast<int *>(ctx); }, &count);
}

esp_err_t orchestrator_state_set_thread_neighbor_count(int count) {
    return with_lock([](void *ctx) { state.thread.neighbor_count = *static_cast<int *>(ctx); }, &count);
}

esp_err_t orchestrator_state_set_thread_border_router_initialized(bool initialized) {
    return with_lock([](void *ctx) { state.thread.border_router_initialized = *static_cast<bool *>(ctx); }, &initialized);
}

esp_err_t orchestrator_state_set_thread_last_cli_error(const char *error) {
    return with_lock([](void *ctx) {
        copy_string(state.thread.last_cli_error, static_cast<const char *>(ctx), sizeof(state.thread.last_cli_error));
    }, const_cast<char *>(error));
}

esp_err_t orchestrator_state_set_matter_platform_initialized(bool initialized) {
    return with_lock([](void *ctx) {
        state.matter.platform_initialized = *static_cast<bool *>(ctx);
        if (state.matter.platform_initialized) {
            state.matter.platform_error[0] = '\0';
            state.matter.last_error[0] = '\0';
        }
    }, &initialized);
}

esp_err_t orchestrator_state_set_matter_platform_error(const char *error) {
    return with_lock([](void *ctx) {
        state.matter.platform_initialized = false;
        state.matter.controller_initialized = false;
        copy_string(state.matter.platform_error, static_cast<const char *>(ctx), sizeof(state.matter.platform_error));
        copy_string(state.matter.last_error, static_cast<const char *>(ctx), sizeof(state.matter.last_error));
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

esp_err_t orchestrator_state_set_matter_controller_config(uint64_t node_id, uint64_t fabric_id, uint16_t listen_port) {
    struct Ctx {
        uint64_t node_id;
        uint64_t fabric_id;
        uint16_t listen_port;
    } ctx = {node_id, fabric_id, listen_port};
    return with_lock([](void *raw) {
        auto *ctx = static_cast<Ctx *>(raw);
        snprintf(state.matter.controller_node_id, sizeof(state.matter.controller_node_id), "%llu",
                 static_cast<unsigned long long>(ctx->node_id));
        state.matter.fabric_id = ctx->fabric_id;
        state.matter.listen_port = ctx->listen_port;
    }, &ctx);
}

esp_err_t orchestrator_state_set_matter_last_error(const char *error) {
    return with_lock([](void *ctx) {
        copy_string(state.matter.last_error, static_cast<const char *>(ctx), sizeof(state.matter.last_error));
    }, const_cast<char *>(error));
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
    cJSON_AddBoolToObject(thread, "platform_initialized", snapshot.thread.platform_initialized);
    cJSON_AddBoolToObject(thread, "enabled", snapshot.thread.enabled);
    cJSON_AddBoolToObject(thread, "interface_up", snapshot.thread.interface_up);
    cJSON_AddBoolToObject(thread, "attached", snapshot.thread.attached);
    cJSON_AddStringToObject(thread, "state", snapshot.thread.role[0] ? snapshot.thread.role : "disabled");
    cJSON_AddStringToObject(thread, "role", snapshot.thread.role[0] ? snapshot.thread.role : "disabled");
    cJSON_AddBoolToObject(thread, "dataset_present", snapshot.thread.dataset_present);
    if (snapshot.thread.network_name[0]) cJSON_AddStringToObject(thread, "network_name", snapshot.thread.network_name);
    else cJSON_AddNullToObject(thread, "network_name");
    if (snapshot.thread.channel >= 0) cJSON_AddNumberToObject(thread, "channel", snapshot.thread.channel);
    else cJSON_AddNullToObject(thread, "channel");
    if (snapshot.thread.pan_id >= 0) cJSON_AddNumberToObject(thread, "pan_id", snapshot.thread.pan_id);
    else cJSON_AddNullToObject(thread, "pan_id");
    if (snapshot.thread.extended_pan_id[0]) cJSON_AddStringToObject(thread, "extended_pan_id", snapshot.thread.extended_pan_id);
    else cJSON_AddNullToObject(thread, "extended_pan_id");
    if (snapshot.thread.mesh_local_prefix[0]) cJSON_AddStringToObject(thread, "mesh_local_prefix", snapshot.thread.mesh_local_prefix);
    else cJSON_AddNullToObject(thread, "mesh_local_prefix");
    if (snapshot.thread.active_timestamp[0]) cJSON_AddStringToObject(thread, "active_timestamp", snapshot.thread.active_timestamp);
    else cJSON_AddNullToObject(thread, "active_timestamp");
    if (snapshot.thread.security_policy[0]) cJSON_AddStringToObject(thread, "security_policy", snapshot.thread.security_policy);
    else cJSON_AddNullToObject(thread, "security_policy");
    if (snapshot.thread.rloc16[0]) cJSON_AddStringToObject(thread, "rloc16", snapshot.thread.rloc16);
    else cJSON_AddNullToObject(thread, "rloc16");
    if (snapshot.thread.ext_address[0]) cJSON_AddStringToObject(thread, "ext_address", snapshot.thread.ext_address);
    else cJSON_AddNullToObject(thread, "ext_address");
    if (snapshot.thread.eui64[0]) cJSON_AddStringToObject(thread, "eui64", snapshot.thread.eui64);
    else cJSON_AddNullToObject(thread, "eui64");
    if (snapshot.thread.leader_data_present) {
        cJSON *leader = cJSON_AddObjectToObject(thread, "leader_data");
        cJSON_AddNumberToObject(leader, "partition_id", snapshot.thread.leader_partition_id);
        cJSON_AddNumberToObject(leader, "weighting", snapshot.thread.leader_weighting);
        cJSON_AddNumberToObject(leader, "data_version", snapshot.thread.leader_data_version);
        cJSON_AddNumberToObject(leader, "stable_data_version", snapshot.thread.leader_stable_data_version);
        cJSON_AddNumberToObject(leader, "leader_router_id", snapshot.thread.leader_router_id);
    } else {
        cJSON_AddNullToObject(thread, "leader_data");
    }
    if (snapshot.thread.unicast_address_count >= 0) cJSON_AddNumberToObject(thread, "unicast_address_count", snapshot.thread.unicast_address_count);
    else cJSON_AddNullToObject(thread, "unicast_address_count");
    if (snapshot.thread.multicast_address_count >= 0) cJSON_AddNumberToObject(thread, "multicast_address_count", snapshot.thread.multicast_address_count);
    else cJSON_AddNullToObject(thread, "multicast_address_count");
    if (snapshot.thread.router_count >= 0) cJSON_AddNumberToObject(thread, "router_count", snapshot.thread.router_count);
    else cJSON_AddNullToObject(thread, "router_count");
    if (snapshot.thread.child_count >= 0) cJSON_AddNumberToObject(thread, "child_count", snapshot.thread.child_count);
    else cJSON_AddNullToObject(thread, "child_count");
    if (snapshot.thread.neighbor_count >= 0) cJSON_AddNumberToObject(thread, "neighbor_count", snapshot.thread.neighbor_count);
    else cJSON_AddNullToObject(thread, "neighbor_count");
    cJSON_AddBoolToObject(thread, "border_router_initialized", snapshot.thread.border_router_initialized);
    if (snapshot.thread.last_cli_error[0]) {
        cJSON_AddStringToObject(thread, "last_cli_error", snapshot.thread.last_cli_error);
        cJSON_AddStringToObject(thread, "last_error", snapshot.thread.last_cli_error);
    } else {
        cJSON_AddNullToObject(thread, "last_cli_error");
        cJSON_AddNullToObject(thread, "last_error");
    }

    cJSON *matter = cJSON_AddObjectToObject(root, "matter");
    cJSON_AddBoolToObject(matter, "platform_initialized", snapshot.matter.platform_initialized);
    if (snapshot.matter.platform_error[0]) {
        cJSON_AddStringToObject(matter, "platform_error", snapshot.matter.platform_error);
    } else {
        cJSON_AddNullToObject(matter, "platform_error");
    }
    cJSON_AddBoolToObject(matter, "controller_initialized", snapshot.matter.controller_initialized);
    if (snapshot.matter.controller_node_id[0]) {
        cJSON_AddStringToObject(matter, "controller_node_id", snapshot.matter.controller_node_id);
    } else {
        cJSON_AddNullToObject(matter, "controller_node_id");
    }
    if (snapshot.matter.fabric_id > 0) cJSON_AddNumberToObject(matter, "fabric_id", static_cast<double>(snapshot.matter.fabric_id));
    else cJSON_AddNullToObject(matter, "fabric_id");
    if (snapshot.matter.listen_port > 0) cJSON_AddNumberToObject(matter, "listen_port", snapshot.matter.listen_port);
    else cJSON_AddNullToObject(matter, "listen_port");
    if (snapshot.matter.last_error[0]) cJSON_AddStringToObject(matter, "last_error", snapshot.matter.last_error);
    else cJSON_AddNullToObject(matter, "last_error");
    cJSON *nodes = device_registry_commissioned_nodes_to_json();
    if (nodes) cJSON_AddItemToObject(matter, "commissioned_nodes", nodes);
    else cJSON_AddItemToObject(matter, "commissioned_nodes", cJSON_CreateArray());

    cJSON *websocket = cJSON_AddObjectToObject(root, "websocket");
    cJSON_AddNumberToObject(websocket, "clients", snapshot.websocket.client_count);

    cJSON *storage = nvs_diagnostics_to_json();
    if (storage) cJSON_AddItemToObject(root, "storage", storage);
    else cJSON_AddItemToObject(root, "storage", cJSON_CreateObject());

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
