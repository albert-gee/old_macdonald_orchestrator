#ifndef ORCHESTRATOR_STATE_H
#define ORCHESTRATOR_STATE_H

#include <cJSON.h>
#include <esp_err.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t orchestrator_state_init(void);

esp_err_t orchestrator_state_set_wifi_ap_running(bool running);
esp_err_t orchestrator_state_set_wifi_sta_configured(bool configured);
esp_err_t orchestrator_state_set_wifi_sta_connected(bool connected, const char *ip);
esp_err_t orchestrator_state_set_wifi_rssi(int rssi);
esp_err_t orchestrator_state_set_thread_enabled(bool enabled);
esp_err_t orchestrator_state_set_thread_attached(bool attached);
esp_err_t orchestrator_state_set_thread_role(const char *role);
esp_err_t orchestrator_state_set_thread_dataset_present(bool present);
esp_err_t orchestrator_state_set_matter_controller_initialized(bool initialized);
esp_err_t orchestrator_state_set_matter_commissioned_node_count(size_t count);
esp_err_t orchestrator_state_set_websocket_client_count(size_t count);

cJSON *orchestrator_state_to_json(void);
esp_err_t orchestrator_state_broadcast_snapshot(void);
esp_err_t orchestrator_state_send_snapshot_to_client(int client_fd);

#ifdef __cplusplus
}
#endif

#endif
