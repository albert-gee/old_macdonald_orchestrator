#include "commands/thread_commands.h"

#include "esp_check.h"
#include "thread_util.h"

static const char *TAG = "THREAD_COMMANDS";

esp_err_t execute_thread_network_init_command(const uint16_t channel, const uint16_t pan_id,
                                                  const char *network_name,
                                                  const char *extended_pan_id, const char *mesh_local_prefix,
                                                  const char *network_key, const char *pskc) {
    return thread_dataset_init(channel, pan_id, network_name, extended_pan_id, mesh_local_prefix, network_key, pskc);
}

esp_err_t execute_thread_enable_command() {
    ESP_RETURN_ON_ERROR(ifconfig_up(), TAG, "Failed to bring interface up");
    ESP_RETURN_ON_ERROR(thread_start(), TAG, "Failed to start Thread");
    return ESP_OK;
}

esp_err_t execute_thread_disable_command() {
    ESP_RETURN_ON_ERROR(thread_stop(), TAG, "Failed to stop Thread");
    ESP_RETURN_ON_ERROR(ifconfig_down(), TAG, "Failed to bring interface down");
    return ESP_OK;
}
