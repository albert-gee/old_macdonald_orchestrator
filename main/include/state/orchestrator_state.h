#ifndef ORCHESTRATOR_STATE_H
#define ORCHESTRATOR_STATE_H

#include <cJSON.h>
#include <esp_err.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t orchestrator_state_init(void);

esp_err_t orchestrator_state_set_wifi_ap_running(bool running);
esp_err_t orchestrator_state_set_wifi_sta_configured(bool configured);
esp_err_t orchestrator_state_set_wifi_sta_connected(bool connected, const char *ip);
esp_err_t orchestrator_state_set_wifi_rssi(int rssi);
esp_err_t orchestrator_state_set_thread_enabled(bool enabled);
esp_err_t orchestrator_state_set_thread_platform_initialized(bool initialized);
esp_err_t orchestrator_state_set_thread_interface_up(bool interface_up);
esp_err_t orchestrator_state_set_thread_attached(bool attached);
esp_err_t orchestrator_state_set_thread_role(const char *role);
esp_err_t orchestrator_state_set_thread_dataset_present(bool present);
esp_err_t orchestrator_state_set_thread_dataset_fields(const char *network_name,
                                                       int channel,
                                                       int pan_id,
                                                       const char *extended_pan_id,
                                                       const char *mesh_local_prefix,
                                                       const char *active_timestamp,
                                                       const char *security_policy);
esp_err_t orchestrator_state_set_thread_rloc16(const char *rloc16);
esp_err_t orchestrator_state_set_thread_ext_address(const char *ext_address);
esp_err_t orchestrator_state_set_thread_eui64(const char *eui64);
esp_err_t orchestrator_state_set_thread_leader_data(const cJSON *leader_data);
esp_err_t orchestrator_state_set_thread_unicast_address_count(int count);
esp_err_t orchestrator_state_set_thread_multicast_address_count(int count);
esp_err_t orchestrator_state_set_thread_router_count(int count);
esp_err_t orchestrator_state_set_thread_child_count(int count);
esp_err_t orchestrator_state_set_thread_neighbor_count(int count);
esp_err_t orchestrator_state_set_thread_border_router_initialized(bool initialized);
esp_err_t orchestrator_state_set_thread_last_cli_error(const char *error);
esp_err_t orchestrator_state_set_matter_platform_initialized(bool initialized);
esp_err_t orchestrator_state_set_matter_platform_error(const char *error);
esp_err_t orchestrator_state_get_matter_platform_status(bool *initialized, char *error, size_t error_len);
esp_err_t orchestrator_state_set_matter_controller_initialized(bool initialized);
esp_err_t orchestrator_state_set_matter_controller_config(uint64_t node_id, uint64_t fabric_id, uint16_t listen_port);
esp_err_t orchestrator_state_set_matter_last_error(const char *error);
esp_err_t orchestrator_state_set_matter_commissioned_node_count(size_t count);
esp_err_t orchestrator_state_set_websocket_client_count(size_t count);

cJSON *orchestrator_state_to_json(void);
esp_err_t orchestrator_state_broadcast_snapshot(void);
esp_err_t orchestrator_state_send_snapshot_to_client(int client_fd);
esp_err_t orchestrator_state_broadcast_event(const char *event, cJSON *payload);

#ifdef __cplusplus
}
#endif

#endif
