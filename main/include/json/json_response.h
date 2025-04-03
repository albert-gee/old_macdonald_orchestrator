#ifndef JSON_RESPONSE_H
#define JSON_RESPONSE_H

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

void binary_to_hex_string(const uint8_t *bin, size_t bin_len, char *hex_str, size_t hex_size);

/**
 * @brief Creates a JSON response object.
 *
 * @param command The command type (e.g., "success", "error").
 * @param payload The message payload.
 * @return A JSON-formatted string.
 */
char *create_json_response(const char *command, const char *payload);


char *create_json_error_response(const char *error_message);

char *create_json_info_response(const char *error_message);

char *create_json_set_dataset_response(uint64_t active_timestamp, uint64_t pending_timestamp,
                                       const uint8_t *network_key, const char *network_name,
                                       const uint8_t *extended_pan_id, const char *mesh_local_prefix,
                                       uint32_t delay, uint16_t pan_id, uint16_t channel,
                                       uint16_t wakeup_channel, const uint8_t *pskc);

char *create_json_thread_role_set_response(const char *role);

#ifdef __cplusplus
}
#endif

#endif //JSON_RESPONSE_H
