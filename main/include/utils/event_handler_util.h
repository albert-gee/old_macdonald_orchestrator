#ifndef EVENT_HANDLER_UTIL_H
#define EVENT_HANDLER_UTIL_H
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

void broadcast_info_message(const char *message);

void broadcast_error_message(const char *message);

void broadcast_thread_dataset_active_message(uint64_t active_timestamp, uint64_t pending_timestamp,
                                             const uint8_t *network_key, const char *network_name,
                                             const uint8_t *extended_pan_id, const char *mesh_local_prefix,
                                             uint32_t delay, uint16_t pan_id, uint16_t channel,
                                             uint16_t wakeup_channel, const uint8_t *pskc);

void broadcast_thread_role_set_message(const char *role);

void broadcast_ifconfig_status(const char *status);

void broadcast_thread_status(const char *status);

void broadcast_wifi_sta_status(const char *status);

void broadcast_temperature(const char *value);

#ifdef __cplusplus
}
#endif


#endif //EVENT_HANDLER_UTIL_H
