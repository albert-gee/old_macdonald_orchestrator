#ifndef THREAD_CLI_COMMANDS_H
#define THREAD_CLI_COMMANDS_H

#include <cJSON.h>
#include <esp_err.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    THREAD_CLI_COMMAND_READ_ONLY,
    THREAD_CLI_COMMAND_SAFE_WRITE,
    THREAD_CLI_COMMAND_NETWORK_MUTATION,
    THREAD_CLI_COMMAND_DANGEROUS,
    THREAD_CLI_COMMAND_UNSUPPORTED,
} thread_cli_command_class_t;

esp_err_t thread_cli_service_init(void);

bool thread_cli_is_deferred_action(const char *action);

esp_err_t thread_cli_enqueue_wss_action(int client_fd,
                                        const char *request_id,
                                        const char *action,
                                        const cJSON *payload,
                                        char *error_code,
                                        size_t error_code_len,
                                        char *error_message,
                                        size_t error_message_len);

cJSON *thread_cli_capabilities_json(void);
cJSON *thread_cli_history_json(void);
esp_err_t thread_cli_cancel(const char *request_id);
thread_cli_command_class_t thread_cli_classify_command(const char *line);
const char *thread_cli_command_class_to_string(thread_cli_command_class_t command_class);

#ifdef __cplusplus
}
#endif

#endif
