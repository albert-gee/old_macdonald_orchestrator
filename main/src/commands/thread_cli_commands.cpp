#include "commands/thread_cli_commands.h"

#include "state/orchestrator_state.h"
#include "websocket_server.h"

#include <esp_check.h>
#include <esp_log.h>
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <openthread/cli.h>
#include <openthread/instance.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const char *TAG = "THREAD_CLI";

static constexpr size_t THREAD_CLI_MAX_LINE_LENGTH = 256;
static constexpr size_t THREAD_CLI_MAX_SCRIPT_LINES = 10;
static constexpr size_t THREAD_CLI_REQUEST_ID_LENGTH = 64;
static constexpr size_t THREAD_CLI_ACTION_LENGTH = 64;
static constexpr uint32_t THREAD_CLI_QUEUE_LENGTH = 8;
static constexpr uint32_t THREAD_CLI_TASK_STACK_SIZE = 8192;
static constexpr UBaseType_t THREAD_CLI_TASK_PRIORITY = 5;
static constexpr int THREAD_CLI_DEFAULT_TIMEOUT_MS = 3000;
static constexpr int THREAD_CLI_MAX_TIMEOUT_MS = 30000;
static constexpr size_t THREAD_CLI_HISTORY_MAX = 24;
static constexpr size_t THREAD_CLI_EVENT_CHUNK_SIZE = 768;
static constexpr size_t THREAD_CLI_RESULT_OUTPUT_LIMIT = 1024;

enum ThreadCliResponseKind {
    THREAD_CLI_RESPONSE_RAW,
    THREAD_CLI_RESPONSE_STATE,
    THREAD_CLI_RESPONSE_STATUS,
    THREAD_CLI_RESPONSE_ATTACHED,
    THREAD_CLI_RESPONSE_DATASET,
    THREAD_CLI_RESPONSE_ADDRESSES,
    THREAD_CLI_RESPONSE_MULTICAST_ADDRESSES,
    THREAD_CLI_RESPONSE_SINGLE_VALUE,
    THREAD_CLI_RESPONSE_LEADER_DATA,
    THREAD_CLI_RESPONSE_TABLE,
    THREAD_CLI_RESPONSE_TEXT,
};

struct ThreadCliWork {
    int client_fd;
    char request_id[THREAD_CLI_REQUEST_ID_LENGTH];
    char action[THREAD_CLI_ACTION_LENGTH];
    char line[THREAD_CLI_MAX_LINE_LENGTH];
    char lines[THREAD_CLI_MAX_SCRIPT_LINES][THREAD_CLI_MAX_LINE_LENGTH];
    size_t line_count;
    int timeout_ms;
    bool confirmed;
    thread_cli_command_class_t command_class;
    ThreadCliResponseKind response_kind;
};

struct ThreadCliSession {
    int client_fd;
    char request_id[THREAD_CLI_REQUEST_ID_LENGTH];
    char action[THREAD_CLI_ACTION_LENGTH];
    std::string line;
    std::string output;
    std::string pending_chunk;
    bool done_seen;
    bool error_seen;
    bool cancelled;
    SemaphoreHandle_t done_sem;
};

static QueueHandle_t cli_queue = nullptr;
static TaskHandle_t cli_task_handle = nullptr;
static SemaphoreHandle_t session_mutex = nullptr;
static ThreadCliSession *active_session = nullptr;
static std::vector<std::string> cli_history;
static bool cli_initialized = false;

static void copy_string(char *dest, const char *src, size_t len) {
    if (!dest || len == 0) return;
    if (!src) src = "";
    strncpy(dest, src, len - 1);
    dest[len - 1] = '\0';
}

static std::string trim_copy(const std::string &value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) begin++;
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) end--;
    return value.substr(begin, end - begin);
}

static std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static bool starts_with(const std::string &value, const char *prefix) {
    return value.rfind(prefix, 0) == 0;
}

const char *thread_cli_command_class_to_string(thread_cli_command_class_t command_class) {
    switch (command_class) {
        case THREAD_CLI_COMMAND_READ_ONLY: return "read_only";
        case THREAD_CLI_COMMAND_SAFE_WRITE: return "safe_write";
        case THREAD_CLI_COMMAND_NETWORK_MUTATION: return "network_mutation";
        case THREAD_CLI_COMMAND_DANGEROUS: return "dangerous";
        case THREAD_CLI_COMMAND_UNSUPPORTED: return "unsupported";
    }
    return "unsupported";
}

thread_cli_command_class_t thread_cli_classify_command(const char *line) {
    if (!line) return THREAD_CLI_COMMAND_UNSUPPORTED;
    const std::string normalized = lower_copy(trim_copy(line));
    if (normalized.empty()) return THREAD_CLI_COMMAND_UNSUPPORTED;

    if (normalized == "factoryreset" || normalized == "reset" ||
        normalized == "dataset clear" ||
        starts_with(normalized, "dataset set active") ||
        starts_with(normalized, "dataset mgmtsetcommand active") ||
        starts_with(normalized, "dataset mgmtsetcommand pending") ||
        normalized == "commissioner stop" ||
        starts_with(normalized, "joiner start") ||
        normalized == "macfilter clear" ||
        normalized == "netdata register" ||
        starts_with(normalized, "route add") ||
        starts_with(normalized, "route remove") ||
        starts_with(normalized, "prefix add") ||
        starts_with(normalized, "prefix remove") ||
        starts_with(normalized, "dns remove") ||
        starts_with(normalized, "dns config") ||
        starts_with(normalized, "srp server disable") ||
        starts_with(normalized, "srp client remove") ||
        starts_with(normalized, "srp client clear") ||
        normalized == "counters reset" ||
        normalized == "history clear") {
        return THREAD_CLI_COMMAND_DANGEROUS;
    }

    if (normalized == "dataset init new" ||
        normalized == "dataset commit active" ||
        normalized == "ifconfig up" ||
        normalized == "ifconfig down" ||
        normalized == "thread start" ||
        normalized == "thread stop" ||
        normalized == "br init" ||
        normalized == "br deinit") {
        return THREAD_CLI_COMMAND_SAFE_WRITE;
    }

    if (starts_with(normalized, "dataset ") ||
        starts_with(normalized, "commissioner ") ||
        starts_with(normalized, "joiner ") ||
        starts_with(normalized, "macfilter ") ||
        starts_with(normalized, "netdata ") ||
        starts_with(normalized, "route ") ||
        starts_with(normalized, "prefix ") ||
        starts_with(normalized, "srp ") ||
        starts_with(normalized, "dns ") ||
        starts_with(normalized, "udp ") ||
        starts_with(normalized, "tcp ") ||
        starts_with(normalized, "coap ")) {
        if (normalized == "dataset active" || normalized == "dataset pending" || normalized == "dataset tlvs" ||
            normalized == "netdata show" || normalized == "netdata full" ||
            normalized == "srp server state" || normalized == "nat64 state") {
            return THREAD_CLI_COMMAND_READ_ONLY;
        }
        return THREAD_CLI_COMMAND_NETWORK_MUTATION;
    }

    static const char *const read_only_commands[] = {
        "help",
        "state",
        "version",
        "extaddr",
        "eui64",
        "rloc16",
        "leaderdata",
        "router table",
        "child table",
        "neighbor table",
        "ipaddr",
        "ipmaddr",
        "channel",
        "panid",
        "extpanid",
        "networkname",
        "domainname",
        "mode",
        "counters",
        "history",
        "debug",
        "scan",
    };
    for (const char *command : read_only_commands) {
        if (normalized == command || starts_with(normalized, (std::string(command) + " ").c_str())) {
            return THREAD_CLI_COMMAND_READ_ONLY;
        }
    }

    return THREAD_CLI_COMMAND_NETWORK_MUTATION;
}

static bool append_json_string(cJSON *array, const std::string &value) {
    cJSON *item = cJSON_CreateString(value.c_str());
    if (!item) return false;
    cJSON_AddItemToArray(array, item);
    return true;
}

static esp_err_t send_json_to_client(int client_fd, cJSON *root) {
    char *json = cJSON_PrintUnformatted(root);
    if (!json) return ESP_FAIL;
    esp_err_t err = websocket_send_message_to_client(client_fd, json);
    free(json);
    return err;
}

static esp_err_t emit_cli_event(int client_fd, const char *event, const char *request_id, cJSON *payload_fields) {
    cJSON *root = cJSON_CreateObject();
    cJSON *payload = payload_fields ? payload_fields : cJSON_CreateObject();
    if (!root || !payload) {
        cJSON_Delete(root);
        cJSON_Delete(payload);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "event");
    cJSON_AddStringToObject(root, "event", event);
    cJSON_AddStringToObject(payload, "request_id", request_id);
    cJSON_AddItemToObject(root, "payload", payload);
    esp_err_t err = send_json_to_client(client_fd, root);
    cJSON_Delete(root);
    return err;
}

static std::vector<std::string> take_output_chunks(ThreadCliSession *session, bool force) {
    std::vector<std::string> chunks;
    if (!session_mutex || !session) return chunks;

    xSemaphoreTake(session_mutex, portMAX_DELAY);
    if (!session->pending_chunk.empty() &&
        (force || session->pending_chunk.size() >= THREAD_CLI_EVENT_CHUNK_SIZE)) {
        while (session->pending_chunk.size() > THREAD_CLI_EVENT_CHUNK_SIZE) {
            chunks.push_back(session->pending_chunk.substr(0, THREAD_CLI_EVENT_CHUNK_SIZE));
            session->pending_chunk.erase(0, THREAD_CLI_EVENT_CHUNK_SIZE);
        }
        if (!session->pending_chunk.empty()) {
            chunks.push_back(session->pending_chunk);
            session->pending_chunk.clear();
        }
    }
    xSemaphoreGive(session_mutex);
    return chunks;
}

static void emit_output_chunks(ThreadCliSession *session, bool force) {
    const std::vector<std::string> chunks = take_output_chunks(session, force);
    for (const std::string &chunk : chunks) {
        cJSON *payload = cJSON_CreateObject();
        if (!payload) continue;
        cJSON_AddStringToObject(payload, "chunk", chunk.c_str());
        emit_cli_event(session->client_fd, "thread.cli.output", session->request_id, payload);
    }
}

static bool output_contains_terminal_status(const char *output, bool *error_seen) {
    if (!output) return false;
    if (strstr(output, "Error") != nullptr) {
        if (error_seen) *error_seen = true;
        return true;
    }
    return strstr(output, "Done") != nullptr;
}

static std::string sanitize_cli_text(const char *text) {
    std::string sanitized;
    if (!text) return sanitized;

    for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(text); *cursor; ++cursor) {
        const unsigned char ch = *cursor;
        if (ch == '\r' || ch == '\n' || ch == '\t' || (ch >= 0x20 && ch <= 0x7e)) {
            sanitized.push_back(static_cast<char>(ch));
        } else {
            char escaped[5] = {};
            snprintf(escaped, sizeof(escaped), "\\x%02X", ch);
            sanitized.append(escaped);
        }
    }

    return sanitized;
}

static void append_cli_output(ThreadCliSession *session, const char *text) {
    if (!session || !text || !*text) return;
    if (strcmp(text, "> ") == 0) return;

    const std::string sanitized = sanitize_cli_text(text);
    if (!sanitized.empty()) {
        session->output.append(sanitized);
        session->pending_chunk.append(sanitized);
    }

    bool error_seen = false;
    if (output_contains_terminal_status(text, &error_seen)) {
        session->done_seen = true;
        session->error_seen = error_seen;
        if (session->done_sem) xSemaphoreGive(session->done_sem);
    }
}

static int cli_output_callback(void *context, const char *format, va_list args) {
    char small[512] = {};
    va_list copy;
    va_copy(copy, args);
    int required = vsnprintf(small, sizeof(small), format, copy);
    va_end(copy);
    if (required < 0) return required;

    char *allocated = nullptr;
    const char *text = small;
    if (static_cast<size_t>(required) >= sizeof(small)) {
        allocated = static_cast<char *>(malloc(static_cast<size_t>(required) + 1));
        if (allocated) {
            va_list copy_large;
            va_copy(copy_large, args);
            vsnprintf(allocated, static_cast<size_t>(required) + 1, format, copy_large);
            va_end(copy_large);
            text = allocated;
        }
    }

    if (session_mutex) xSemaphoreTake(session_mutex, portMAX_DELAY);
    append_cli_output(active_session, text);
    if (session_mutex) xSemaphoreGive(session_mutex);

    free(allocated);
    return required;
}

static void add_history(const char *line) {
    if (!line || !*line) return;
    cli_history.emplace_back(line);
    while (cli_history.size() > THREAD_CLI_HISTORY_MAX) {
        cli_history.erase(cli_history.begin());
    }
}

static std::vector<std::string> payload_lines(const std::string &output) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= output.size()) {
        size_t end = output.find('\n', start);
        if (end == std::string::npos) end = output.size();
        std::string line = output.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim_copy(line);
        if (!line.empty() && line != "Done" && !starts_with(line, "Error") && line != ">" && !starts_with(line, "> ")) {
            lines.push_back(line);
        }
        if (end == output.size()) break;
        start = end + 1;
    }
    return lines;
}

static std::string redact_dataset_secrets(const std::string &output) {
    std::string redacted;
    size_t start = 0;
    while (start <= output.size()) {
        size_t end = output.find('\n', start);
        if (end == std::string::npos) end = output.size();
        std::string line = output.substr(start, end - start);
        const std::string lower = lower_copy(line);
        if (lower.find("network key") != std::string::npos || lower.find("pskc") != std::string::npos) {
            const size_t colon = line.find(':');
            redacted += colon == std::string::npos ? "<hidden>" : line.substr(0, colon + 1) + " <hidden>";
        } else {
            redacted += line;
        }
        if (end != output.size()) redacted += '\n';
        if (end == output.size()) break;
        start = end + 1;
    }
    return redacted;
}

static bool output_contains_sensitive_dataset_secret(const std::string &output) {
    const std::string lower = lower_copy(output);
    return lower.find("network key") != std::string::npos ||
           lower.find("pskc") != std::string::npos ||
           lower.find("master key") != std::string::npos;
}

static bool parse_number_value(const std::string &value, int *out) {
    if (!out) return false;
    char *end = nullptr;
    int base = starts_with(lower_copy(value), "0x") ? 16 : 10;
    long parsed = strtol(value.c_str(), &end, base);
    if (end == value.c_str()) return false;
    *out = static_cast<int>(parsed);
    return true;
}

static void add_first_line_value(cJSON *payload, const char *key, const std::string &output) {
    std::vector<std::string> lines = payload_lines(output);
    if (!lines.empty()) cJSON_AddStringToObject(payload, key, lines.front().c_str());
}

static void add_dataset_fields(cJSON *payload, const std::string &output) {
    std::vector<std::string> lines = payload_lines(output);
    cJSON_AddBoolToObject(payload, "present", !lines.empty());

    for (const std::string &line : lines) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = lower_copy(trim_copy(line.substr(0, colon)));
        const std::string value = trim_copy(line.substr(colon + 1));
        int parsed = 0;
        if (key == "active timestamp") {
            cJSON_AddStringToObject(payload, "active_timestamp", value.c_str());
        } else if (key == "network name") {
            cJSON_AddStringToObject(payload, "network_name", value.c_str());
        } else if (key == "channel" && parse_number_value(value, &parsed)) {
            cJSON_AddNumberToObject(payload, "channel", parsed);
        } else if ((key == "pan id" || key == "panid") && parse_number_value(value, &parsed)) {
            cJSON_AddNumberToObject(payload, "pan_id", parsed);
        } else if (key == "ext pan id" || key == "extended pan id") {
            cJSON_AddStringToObject(payload, "extended_pan_id", value.c_str());
        } else if (key == "mesh local prefix" || key == "mesh-local prefix") {
            cJSON_AddStringToObject(payload, "mesh_local_prefix", value.c_str());
        } else if (key == "security policy") {
            cJSON_AddStringToObject(payload, "security_policy", value.c_str());
        }
    }
}

static void add_address_fields(cJSON *payload, const std::string &output, const char *key) {
    cJSON *array = cJSON_AddArrayToObject(payload, key);
    if (!array) return;
    for (const std::string &line : payload_lines(output)) {
        append_json_string(array, line);
    }
}

static void add_leader_data_fields(cJSON *payload, const std::string &output) {
    cJSON *leader = cJSON_AddObjectToObject(payload, "leader_data");
    if (!leader) return;
    for (const std::string &line : payload_lines(output)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = lower_copy(trim_copy(line.substr(0, colon)));
        const std::string value = trim_copy(line.substr(colon + 1));
        int parsed = 0;
        if (!parse_number_value(value, &parsed)) continue;
        if (key == "partition id") cJSON_AddNumberToObject(leader, "partition_id", parsed);
        else if (key == "weighting") cJSON_AddNumberToObject(leader, "weighting", parsed);
        else if (key == "data version") cJSON_AddNumberToObject(leader, "data_version", parsed);
        else if (key == "stable data version") cJSON_AddNumberToObject(leader, "stable_data_version", parsed);
        else if (key == "leader router id") cJSON_AddNumberToObject(leader, "leader_router_id", parsed);
    }
}

static void add_table_fields(cJSON *payload, const std::string &output, const char *count_key) {
    const std::vector<std::string> lines = payload_lines(output);
    cJSON *array = cJSON_AddArrayToObject(payload, "rows");
    if (!array) return;
    int count = 0;
    for (const std::string &line : lines) {
        append_json_string(array, line);
        if (line.find('|') != std::string::npos && line.find("---") == std::string::npos) count++;
    }
    cJSON_AddNumberToObject(payload, count_key, count);
}

static cJSON *build_payload_for_work(const ThreadCliWork &work, const std::string &output) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return nullptr;

    const std::string visible_output = work.response_kind == THREAD_CLI_RESPONSE_DATASET
        ? redact_dataset_secrets(output)
        : output;
    std::string result_output = visible_output;
    const bool output_truncated = result_output.size() > THREAD_CLI_RESULT_OUTPUT_LIMIT;
    if (output_truncated) {
        result_output.resize(THREAD_CLI_RESULT_OUTPUT_LIMIT);
        result_output += "\n... output truncated in command_result; use thread.cli.output events for full output ...\n";
    }
    cJSON_AddStringToObject(payload, "line", work.line);
    cJSON_AddStringToObject(payload, "output", result_output.c_str());
    cJSON_AddNumberToObject(payload, "output_bytes", visible_output.size());
    cJSON_AddBoolToObject(payload, "output_truncated", output_truncated);
    cJSON_AddBoolToObject(payload, "sensitive", output_contains_sensitive_dataset_secret(output));
    cJSON_AddStringToObject(payload, "status", "done");
    cJSON_AddStringToObject(payload, "command_class", thread_cli_command_class_to_string(work.command_class));

    switch (work.response_kind) {
        case THREAD_CLI_RESPONSE_STATE:
            add_first_line_value(payload, "state", output);
            add_first_line_value(payload, "role", output);
            break;
        case THREAD_CLI_RESPONSE_STATUS:
            add_first_line_value(payload, "state", output);
            {
                const std::vector<std::string> lines = payload_lines(output);
                const std::string state = lines.empty() ? "unknown" : lower_copy(lines.front());
                cJSON_AddBoolToObject(payload, "running", state != "disabled");
                cJSON_AddBoolToObject(payload, "enabled", state != "disabled");
            }
            break;
        case THREAD_CLI_RESPONSE_ATTACHED:
            add_first_line_value(payload, "state", output);
            {
                const std::vector<std::string> lines = payload_lines(output);
                const std::string state = lines.empty() ? "unknown" : lower_copy(lines.front());
                cJSON_AddBoolToObject(payload, "attached", state == "child" || state == "router" || state == "leader");
            }
            break;
        case THREAD_CLI_RESPONSE_DATASET:
            add_dataset_fields(payload, output);
            break;
        case THREAD_CLI_RESPONSE_ADDRESSES:
            add_address_fields(payload, output, "unicast");
            break;
        case THREAD_CLI_RESPONSE_MULTICAST_ADDRESSES:
            add_address_fields(payload, output, "multicast");
            break;
        case THREAD_CLI_RESPONSE_SINGLE_VALUE:
            if (strcmp(work.line, "rloc16") == 0) add_first_line_value(payload, "rloc16", output);
            else if (strcmp(work.line, "extaddr") == 0) add_first_line_value(payload, "ext_address", output);
            else if (strcmp(work.line, "eui64") == 0) add_first_line_value(payload, "eui64", output);
            else add_first_line_value(payload, "value", output);
            break;
        case THREAD_CLI_RESPONSE_LEADER_DATA:
            add_leader_data_fields(payload, output);
            break;
        case THREAD_CLI_RESPONSE_TABLE:
            if (strcmp(work.line, "router table") == 0) add_table_fields(payload, output, "router_count");
            else if (strcmp(work.line, "child table") == 0) add_table_fields(payload, output, "child_count");
            else if (strcmp(work.line, "neighbor table") == 0) add_table_fields(payload, output, "neighbor_count");
            else add_table_fields(payload, output, "row_count");
            break;
        case THREAD_CLI_RESPONSE_TEXT:
        case THREAD_CLI_RESPONSE_RAW:
            break;
    }

    return payload;
}

static esp_err_t send_command_result_success(int client_fd, const char *request_id, const char *action, cJSON *payload) {
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

static esp_err_t send_command_result_error(int client_fd,
                                           const char *request_id,
                                           const char *action,
                                           const char *code,
                                           const char *message,
                                           const char *line,
                                           const std::string &output) {
    cJSON *root = cJSON_CreateObject();
    cJSON *error = cJSON_CreateObject();
    cJSON *details = cJSON_CreateObject();
    if (!root || !error || !details) {
        cJSON_Delete(root);
        cJSON_Delete(error);
        cJSON_Delete(details);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "command_result");
    cJSON_AddStringToObject(root, "request_id", request_id);
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    cJSON_AddStringToObject(details, "line", line ? line : "");
    cJSON_AddStringToObject(details, "output", output.c_str());
    cJSON_AddItemToObject(error, "details", details);
    cJSON_AddItemToObject(root, "error", error);
    esp_err_t err = send_json_to_client(client_fd, root);
    cJSON_Delete(root);
    return err;
}

static void update_snapshot_from_payload(const ThreadCliWork &work, cJSON *payload) {
    if (!payload) return;
    if (strcmp(work.action, "thread.enable") == 0) {
        orchestrator_state_set_thread_interface_up(true);
        orchestrator_state_set_thread_enabled(true);
    } else if (strcmp(work.action, "thread.disable") == 0) {
        orchestrator_state_set_thread_enabled(false);
        orchestrator_state_set_thread_interface_up(false);
        orchestrator_state_set_thread_attached(false);
        orchestrator_state_set_thread_role("disabled");
    }
    if (work.response_kind == THREAD_CLI_RESPONSE_STATE ||
        work.response_kind == THREAD_CLI_RESPONSE_STATUS ||
        work.response_kind == THREAD_CLI_RESPONSE_ATTACHED) {
        const cJSON *state_item = cJSON_GetObjectItem(payload, "state");
        const char *role = cJSON_IsString(state_item) ? state_item->valuestring : nullptr;
        if (role) {
            orchestrator_state_set_thread_role(role);
            orchestrator_state_set_thread_enabled(strcmp(role, "disabled") != 0);
            orchestrator_state_set_thread_attached(strcmp(role, "child") == 0 ||
                                                   strcmp(role, "router") == 0 ||
                                                   strcmp(role, "leader") == 0);
        }
    } else if (work.response_kind == THREAD_CLI_RESPONSE_DATASET) {
        const cJSON *present = cJSON_GetObjectItem(payload, "present");
        orchestrator_state_set_thread_dataset_present(cJSON_IsTrue(present));
        const cJSON *network_name = cJSON_GetObjectItem(payload, "network_name");
        const cJSON *channel = cJSON_GetObjectItem(payload, "channel");
        const cJSON *pan_id = cJSON_GetObjectItem(payload, "pan_id");
        const cJSON *ext_pan_id = cJSON_GetObjectItem(payload, "extended_pan_id");
        const cJSON *mesh_prefix = cJSON_GetObjectItem(payload, "mesh_local_prefix");
        const cJSON *active_ts = cJSON_GetObjectItem(payload, "active_timestamp");
        const cJSON *security = cJSON_GetObjectItem(payload, "security_policy");
        orchestrator_state_set_thread_dataset_fields(
            cJSON_IsString(network_name) ? network_name->valuestring : nullptr,
            cJSON_IsNumber(channel) ? channel->valueint : -1,
            cJSON_IsNumber(pan_id) ? pan_id->valueint : -1,
            cJSON_IsString(ext_pan_id) ? ext_pan_id->valuestring : nullptr,
            cJSON_IsString(mesh_prefix) ? mesh_prefix->valuestring : nullptr,
            cJSON_IsString(active_ts) ? active_ts->valuestring : nullptr,
            cJSON_IsString(security) ? security->valuestring : nullptr);
    } else if (work.response_kind == THREAD_CLI_RESPONSE_SINGLE_VALUE && strcmp(work.line, "rloc16") == 0) {
        const cJSON *rloc16 = cJSON_GetObjectItem(payload, "rloc16");
        if (cJSON_IsString(rloc16)) orchestrator_state_set_thread_rloc16(rloc16->valuestring);
    } else if (work.response_kind == THREAD_CLI_RESPONSE_SINGLE_VALUE && strcmp(work.line, "extaddr") == 0) {
        const cJSON *ext_address = cJSON_GetObjectItem(payload, "ext_address");
        if (cJSON_IsString(ext_address)) orchestrator_state_set_thread_ext_address(ext_address->valuestring);
    } else if (work.response_kind == THREAD_CLI_RESPONSE_SINGLE_VALUE && strcmp(work.line, "eui64") == 0) {
        const cJSON *eui64 = cJSON_GetObjectItem(payload, "eui64");
        if (cJSON_IsString(eui64)) orchestrator_state_set_thread_eui64(eui64->valuestring);
    } else if (work.response_kind == THREAD_CLI_RESPONSE_ADDRESSES) {
        const cJSON *array = cJSON_GetObjectItem(payload, "unicast");
        orchestrator_state_set_thread_unicast_address_count(cJSON_IsArray(array) ? cJSON_GetArraySize(array) : 0);
    } else if (work.response_kind == THREAD_CLI_RESPONSE_MULTICAST_ADDRESSES) {
        const cJSON *array = cJSON_GetObjectItem(payload, "multicast");
        orchestrator_state_set_thread_multicast_address_count(cJSON_IsArray(array) ? cJSON_GetArraySize(array) : 0);
    } else if (work.response_kind == THREAD_CLI_RESPONSE_TABLE) {
        if (strcmp(work.line, "router table") == 0) {
            const cJSON *count = cJSON_GetObjectItem(payload, "router_count");
            if (cJSON_IsNumber(count)) orchestrator_state_set_thread_router_count(count->valueint);
        } else if (strcmp(work.line, "child table") == 0) {
            const cJSON *count = cJSON_GetObjectItem(payload, "child_count");
            if (cJSON_IsNumber(count)) orchestrator_state_set_thread_child_count(count->valueint);
        } else if (strcmp(work.line, "neighbor table") == 0) {
            const cJSON *count = cJSON_GetObjectItem(payload, "neighbor_count");
            if (cJSON_IsNumber(count)) orchestrator_state_set_thread_neighbor_count(count->valueint);
        }
    } else if (work.response_kind == THREAD_CLI_RESPONSE_LEADER_DATA) {
        const cJSON *leader = cJSON_GetObjectItem(payload, "leader_data");
        if (cJSON_IsObject(leader)) orchestrator_state_set_thread_leader_data(leader);
    }
}

static bool should_broadcast_snapshot_after_work(const ThreadCliWork &work) {
    if (strcmp(work.action, "thread.enable") == 0 ||
        strcmp(work.action, "thread.disable") == 0 ||
        strcmp(work.action, "thread.dataset.init") == 0 ||
        strcmp(work.action, "thread.dataset.init_new") == 0 ||
        strcmp(work.action, "thread.dataset.commit_active") == 0 ||
        strcmp(work.action, "thread.dataset.clear_buffer") == 0 ||
        strcmp(work.action, "thread.interface_up") == 0 ||
        strcmp(work.action, "thread.interface_down") == 0 ||
        strcmp(work.action, "thread.br_init") == 0 ||
        strcmp(work.action, "thread.br_deinit") == 0) {
        return true;
    }
    if (strcmp(work.action, "thread.cli.exec") == 0 ||
        strcmp(work.action, "thread.cli.help") == 0) {
        return work.command_class != THREAD_CLI_COMMAND_READ_ONLY;
    }
    if (strcmp(work.action, "thread.cli.capabilities") == 0 ||
        strcmp(work.action, "thread.cli.history") == 0 ||
        strcmp(work.action, "thread.cli.cancel") == 0) {
        return false;
    }
    return false;
}

static void thread_cli_worker(void *context) {
    ThreadCliWork *work_storage = static_cast<ThreadCliWork *>(calloc(1, sizeof(ThreadCliWork)));
    if (!work_storage) {
        ESP_LOGE(TAG, "Failed to allocate Thread CLI worker storage");
        vTaskDelete(nullptr);
        return;
    }

    while (xQueueReceive(cli_queue, work_storage, portMAX_DELAY) == pdTRUE) {
        ThreadCliWork &work = *work_storage;
        ESP_LOGI(TAG, "Executing CLI command for action=%s line=%s", work.action, work.line);
        ThreadCliSession session = {};
        session.client_fd = work.client_fd;
        session.line = work.line;
        session.done_sem = xSemaphoreCreateBinary();
        copy_string(session.request_id, work.request_id, sizeof(session.request_id));
        copy_string(session.action, work.action, sizeof(session.action));

        if (!session.done_sem) {
            send_command_result_error(work.client_fd, work.request_id, work.action,
                                      "THREAD_CLI_NO_MEMORY",
                                      "OpenThread CLI session allocation failed.",
                                      work.line, "");
            continue;
        }

        xSemaphoreTake(session_mutex, portMAX_DELAY);
        active_session = &session;
        xSemaphoreGive(session_mutex);

        bool lock_acquired = true;
        for (size_t i = 0; i < work.line_count; ++i) {
            session.done_seen = false;
            session.error_seen = false;
            char line_copy[THREAD_CLI_MAX_LINE_LENGTH] = {};
            copy_string(line_copy, work.lines[i], sizeof(line_copy));
            session.output.append("> ");
            session.output.append(line_copy);
            session.output.append("\n");

            lock_acquired = esp_openthread_lock_acquire(pdMS_TO_TICKS(work.timeout_ms));
            if (lock_acquired) {
                otCliInputLine(line_copy);
                esp_openthread_lock_release();
            } else {
                session.output.append("OpenThread lock timeout\n");
                session.error_seen = true;
            }
            emit_output_chunks(&session, false);

            if (!session.done_seen && !session.error_seen && lock_acquired) {
                xSemaphoreTake(session.done_sem, pdMS_TO_TICKS(work.timeout_ms));
            }
            emit_output_chunks(&session, false);
            if (!lock_acquired || session.error_seen || !session.done_seen || session.cancelled) {
                break;
            }
        }

        emit_output_chunks(&session, true);
        xSemaphoreTake(session_mutex, portMAX_DELAY);
        active_session = nullptr;
        xSemaphoreGive(session_mutex);

        if (session.cancelled) {
            send_command_result_error(work.client_fd, work.request_id, work.action,
                                      "THREAD_CLI_CANCELLED",
                                      "OpenThread CLI command was cancelled.",
                                      work.line, session.output);
        } else if (!lock_acquired) {
            orchestrator_state_set_thread_last_cli_error("OpenThread lock timeout");
            send_command_result_error(work.client_fd, work.request_id, work.action,
                                      "THREAD_CLI_TIMEOUT",
                                      "Timed out waiting for OpenThread lock.",
                                      work.line, session.output);
        } else if (!session.done_seen && !session.error_seen) {
            cJSON *payload = cJSON_CreateObject();
            if (payload) {
                cJSON_AddStringToObject(payload, "status", "timeout");
                emit_cli_event(work.client_fd, "thread.cli.done", work.request_id, payload);
            }
            orchestrator_state_set_thread_last_cli_error("OpenThread CLI command timed out");
            send_command_result_error(work.client_fd, work.request_id, work.action,
                                      "THREAD_CLI_TIMEOUT",
                                      "OpenThread CLI command timed out.",
                                      work.line, session.output);
        } else if (session.error_seen) {
            cJSON *payload = cJSON_CreateObject();
            if (payload) {
                cJSON_AddStringToObject(payload, "status", "error");
                emit_cli_event(work.client_fd, "thread.cli.done", work.request_id, payload);
            }
            orchestrator_state_set_thread_last_cli_error(session.output.c_str());
            send_command_result_error(work.client_fd, work.request_id, work.action,
                                      "THREAD_CLI_ERROR",
                                      "OpenThread CLI returned an error.",
                                      work.line, session.output);
        } else {
            cJSON *payload = cJSON_CreateObject();
            if (payload) {
                cJSON_AddStringToObject(payload, "status", "done");
                emit_cli_event(work.client_fd, "thread.cli.done", work.request_id, payload);
            }
            cJSON *result_payload = build_payload_for_work(work, session.output);
            update_snapshot_from_payload(work, result_payload);
            orchestrator_state_set_thread_last_cli_error(nullptr);
            send_command_result_success(work.client_fd, work.request_id, work.action, result_payload);
            add_history(work.line);
            if (should_broadcast_snapshot_after_work(work)) {
                orchestrator_state_broadcast_snapshot();
            }
        }

        vSemaphoreDelete(session.done_sem);
        memset(work_storage, 0, sizeof(ThreadCliWork));
    }

    free(work_storage);
}

static ThreadCliResponseKind response_kind_for_action(const char *action, const char *line) {
    if (!action) return THREAD_CLI_RESPONSE_RAW;
    if (strcmp(action, "thread.status_get") == 0) return THREAD_CLI_RESPONSE_STATUS;
    if (strcmp(action, "thread.attached_get") == 0) return THREAD_CLI_RESPONSE_ATTACHED;
    if (strcmp(action, "thread.role_get") == 0 || strcmp(action, "thread.state_get") == 0) return THREAD_CLI_RESPONSE_STATE;
    if (strcmp(action, "thread.active_dataset_get") == 0 ||
        strcmp(action, "thread.pending_dataset_get") == 0) {
        return THREAD_CLI_RESPONSE_DATASET;
    }
    if (strcmp(action, "thread.unicast_addresses_get") == 0) {
        return THREAD_CLI_RESPONSE_ADDRESSES;
    }
    if (strcmp(action, "thread.multicast_addresses_get") == 0) {
        return THREAD_CLI_RESPONSE_MULTICAST_ADDRESSES;
    }
    if (strcmp(action, "thread.rloc16_get") == 0 ||
        strcmp(action, "thread.extaddr_get") == 0 ||
        strcmp(action, "thread.eui64_get") == 0) {
        return THREAD_CLI_RESPONSE_SINGLE_VALUE;
    }
    if (strcmp(action, "thread.leader_data_get") == 0) return THREAD_CLI_RESPONSE_LEADER_DATA;
    if (strcmp(action, "thread.router_table_get") == 0 ||
        strcmp(action, "thread.child_table_get") == 0 ||
        strcmp(action, "thread.neighbor_table_get") == 0) {
        return THREAD_CLI_RESPONSE_TABLE;
    }
    if (line && (strcmp(line, "netdata show") == 0 || strcmp(line, "netdata full") == 0 || strcmp(line, "counters") == 0)) {
        return THREAD_CLI_RESPONSE_TEXT;
    }
    return THREAD_CLI_RESPONSE_RAW;
}

static bool mapped_line_for_action(const char *action, const cJSON *payload, char *line, size_t line_len) {
    if (!action || !line || line_len == 0) return false;
    if (strcmp(action, "thread.cli.exec") == 0) {
        if (!payload) return true;
        const cJSON *line_item = cJSON_GetObjectItem(payload, "line");
        if (!cJSON_IsString(line_item) || !line_item->valuestring) return false;
        copy_string(line, line_item->valuestring, line_len);
        return true;
    }
    if (strcmp(action, "thread.cli.help") == 0) {
        copy_string(line, "help", line_len);
        return true;
    }

    struct Mapping { const char *action; const char *line; };
    static const Mapping mappings[] = {
        {"thread.enable", "ifconfig up"},
        {"thread.disable", "thread stop"},
        {"thread.status_get", "state"},
        {"thread.attached_get", "state"},
        {"thread.role_get", "state"},
        {"thread.state_get", "state"},
        {"thread.active_dataset_get", "dataset active"},
        {"thread.pending_dataset_get", "dataset pending"},
        {"thread.dataset.init_new", "dataset init new"},
        {"thread.dataset.commit_active", "dataset commit active"},
        {"thread.dataset.clear_buffer", "dataset clear"},
        {"thread.interface_up", "ifconfig up"},
        {"thread.interface_down", "ifconfig down"},
        {"thread.rloc16_get", "rloc16"},
        {"thread.extaddr_get", "extaddr"},
        {"thread.eui64_get", "eui64"},
        {"thread.leader_data_get", "leaderdata"},
        {"thread.router_table_get", "router table"},
        {"thread.child_table_get", "child table"},
        {"thread.neighbor_table_get", "neighbor table"},
        {"thread.unicast_addresses_get", "ipaddr"},
        {"thread.multicast_addresses_get", "ipmaddr"},
        {"thread.netdata_get", "netdata show"},
        {"thread.counters_get", "counters"},
        {"thread.history_get", "history"},
        {"thread.topology_get", "router table"},
        {"thread.available_networks_scan", "scan"},
    };
    for (const Mapping &mapping : mappings) {
        if (strcmp(action, mapping.action) == 0) {
            copy_string(line, mapping.line, line_len);
            return true;
        }
    }
    return false;
}

static bool value_has_line_break(const char *value) {
    return value && (strchr(value, '\n') || strchr(value, '\r'));
}

static bool required_payload_string(const cJSON *payload, const char *name, const char **out) {
    const cJSON *item = cJSON_GetObjectItem(payload, name);
    if (!cJSON_IsString(item) || !item->valuestring || !item->valuestring[0]) return false;
    if (value_has_line_break(item->valuestring)) return false;
    *out = item->valuestring;
    return true;
}

static bool add_work_line(ThreadCliWork *work, const char *line) {
    if (!work || !line || !*line || work->line_count >= THREAD_CLI_MAX_SCRIPT_LINES) return false;
    if (strlen(line) >= THREAD_CLI_MAX_LINE_LENGTH) return false;
    copy_string(work->lines[work->line_count], line, sizeof(work->lines[work->line_count]));
    work->line_count++;
    if (work->line[0] == '\0') copy_string(work->line, line, sizeof(work->line));
    return true;
}

static bool populate_work_lines(const char *action, const cJSON *payload, ThreadCliWork *work) {
    if (!action || !work) return false;

    if (strcmp(action, "thread.enable") == 0) {
        return add_work_line(work, "ifconfig up") && add_work_line(work, "thread start");
    }
    if (strcmp(action, "thread.disable") == 0) {
        return add_work_line(work, "thread stop") && add_work_line(work, "ifconfig down");
    }
    if (strcmp(action, "thread.dataset.init") == 0) {
        const cJSON *channel = cJSON_GetObjectItem(payload, "channel");
        const cJSON *pan_id = cJSON_GetObjectItem(payload, "pan_id");
        const char *network_name = nullptr;
        const char *extended_pan_id = nullptr;
        const char *mesh_local_prefix = nullptr;
        const char *network_key = nullptr;
        const char *pskc = nullptr;
        if (!cJSON_IsNumber(channel) || !cJSON_IsNumber(pan_id) ||
            !required_payload_string(payload, "network_name", &network_name) ||
            !required_payload_string(payload, "extended_pan_id", &extended_pan_id) ||
            !required_payload_string(payload, "mesh_local_prefix", &mesh_local_prefix) ||
            !required_payload_string(payload, "master_key", &network_key) ||
            !required_payload_string(payload, "pskc", &pskc)) {
            return false;
        }
        char command[THREAD_CLI_MAX_LINE_LENGTH] = {};
        if (!add_work_line(work, "dataset init new")) return false;
        snprintf(command, sizeof(command), "dataset networkname %s", network_name);
        if (!add_work_line(work, command)) return false;
        snprintf(command, sizeof(command), "dataset channel %d", channel->valueint);
        if (!add_work_line(work, command)) return false;
        snprintf(command, sizeof(command), "dataset panid 0x%04X", pan_id->valueint & 0xFFFF);
        if (!add_work_line(work, command)) return false;
        snprintf(command, sizeof(command), "dataset extpanid %s", extended_pan_id);
        if (!add_work_line(work, command)) return false;
        snprintf(command, sizeof(command), "dataset meshlocalprefix %s", mesh_local_prefix);
        if (!add_work_line(work, command)) return false;
        snprintf(command, sizeof(command), "dataset networkkey %s", network_key);
        if (!add_work_line(work, command)) return false;
        snprintf(command, sizeof(command), "dataset pskc %s", pskc);
        if (!add_work_line(work, command)) return false;
        return add_work_line(work, "dataset commit active");
    }

    char line[THREAD_CLI_MAX_LINE_LENGTH] = {};
    if (!mapped_line_for_action(action, payload, line, sizeof(line))) return false;
    return add_work_line(work, line);
}

bool thread_cli_is_deferred_action(const char *action) {
    if (!action) return false;
    if (strcmp(action, "thread.cli.exec") == 0 ||
        strcmp(action, "thread.cli.help") == 0 ||
        strcmp(action, "thread.enable") == 0 ||
        strcmp(action, "thread.disable") == 0 ||
        strcmp(action, "thread.dataset.init") == 0) {
        return true;
    }
    if (strcmp(action, "thread.cli.capabilities") == 0 ||
        strcmp(action, "thread.cli.history") == 0 ||
        strcmp(action, "thread.cli.cancel") == 0) {
        return false;
    }
    char line[THREAD_CLI_MAX_LINE_LENGTH] = {};
    return mapped_line_for_action(action, nullptr, line, sizeof(line));
}

static int timeout_from_payload(const cJSON *payload) {
    const cJSON *timeout = cJSON_GetObjectItem(payload, "timeout_ms");
    int timeout_ms = cJSON_IsNumber(timeout) ? timeout->valueint : THREAD_CLI_DEFAULT_TIMEOUT_MS;
    if (timeout_ms <= 0) timeout_ms = THREAD_CLI_DEFAULT_TIMEOUT_MS;
    if (timeout_ms > THREAD_CLI_MAX_TIMEOUT_MS) timeout_ms = THREAD_CLI_MAX_TIMEOUT_MS;
    return timeout_ms;
}

esp_err_t thread_cli_enqueue_wss_action(int client_fd,
                                        const char *request_id,
                                        const char *action,
                                        const cJSON *payload,
                                        char *error_code,
                                        size_t error_code_len,
                                        char *error_message,
                                        size_t error_message_len) {
    if (!request_id || !action || !payload) {
        copy_string(error_code, "INVALID_THREAD_CLI_REQUEST", error_code_len);
        copy_string(error_message, "Invalid Thread CLI request.", error_message_len);
        return ESP_ERR_INVALID_ARG;
    }
    if (!cli_queue) {
        copy_string(error_code, "THREAD_CLI_NOT_INITIALIZED", error_code_len);
        copy_string(error_message, "Thread CLI service is not initialized.", error_message_len);
        return ESP_ERR_INVALID_STATE;
    }

    ThreadCliWork work = {};
    work.client_fd = client_fd;
    copy_string(work.request_id, request_id, sizeof(work.request_id));
    copy_string(work.action, action, sizeof(work.action));
    work.timeout_ms = timeout_from_payload(payload);
    const cJSON *confirmed = cJSON_GetObjectItem(payload, "confirmed");
    work.confirmed = cJSON_IsTrue(confirmed);

    if (!populate_work_lines(action, payload, &work)) {
        copy_string(error_code, "INVALID_THREAD_CLI_REQUEST", error_code_len);
        copy_string(error_message, "Thread CLI action requires a valid command line.", error_message_len);
        return ESP_ERR_INVALID_ARG;
    }

    work.command_class = THREAD_CLI_COMMAND_READ_ONLY;
    std::string display_line;
    for (size_t i = 0; i < work.line_count; ++i) {
        const std::string line = trim_copy(work.lines[i]);
        if (line.empty()) {
            copy_string(error_code, "THREAD_CLI_EMPTY_LINE", error_code_len);
            copy_string(error_message, "OpenThread CLI command line must not be empty.", error_message_len);
            return ESP_ERR_INVALID_ARG;
        }
        if (line.size() >= THREAD_CLI_MAX_LINE_LENGTH || line.find('\n') != std::string::npos || line.find('\r') != std::string::npos) {
            copy_string(error_code, "THREAD_CLI_INVALID_LINE", error_code_len);
            copy_string(error_message, "OpenThread CLI command line is too long or contains a newline.", error_message_len);
            return ESP_ERR_INVALID_ARG;
        }
        copy_string(work.lines[i], line.c_str(), sizeof(work.lines[i]));
        if (!display_line.empty()) display_line += "; ";
        display_line += line;
        thread_cli_command_class_t line_class = thread_cli_classify_command(line.c_str());
        if (line_class > work.command_class) work.command_class = line_class;
    }
    copy_string(work.line, display_line.c_str(), sizeof(work.line));

    const bool raw_cli_exec = strcmp(action, "thread.cli.exec") == 0;
    const cJSON *dangerous_confirm_text = cJSON_GetObjectItem(payload, "dangerous_confirm_text");
    const bool strong_confirmation =
        work.confirmed &&
        cJSON_IsString(dangerous_confirm_text) &&
        strcmp(dangerous_confirm_text->valuestring, "I understand") == 0;

    if (work.command_class == THREAD_CLI_COMMAND_DANGEROUS && !strong_confirmation) {
        copy_string(error_code, "THREAD_CLI_CONFIRMATION_REQUIRED", error_code_len);
        copy_string(error_message, "This OpenThread command can disrupt or reset the network.", error_message_len);
        return ESP_ERR_INVALID_ARG;
    }
    if (raw_cli_exec && work.command_class == THREAD_CLI_COMMAND_NETWORK_MUTATION && !work.confirmed) {
        copy_string(error_code, "THREAD_CLI_CONFIRMATION_REQUIRED", error_code_len);
        copy_string(error_message, "This OpenThread command changes network state and requires confirmation.", error_message_len);
        return ESP_ERR_INVALID_ARG;
    }
    work.response_kind = response_kind_for_action(action, work.line);

    if (xQueueSend(cli_queue, &work, pdMS_TO_TICKS(100)) != pdTRUE) {
        copy_string(error_code, "THREAD_CLI_BUSY", error_code_len);
        copy_string(error_message, "Thread CLI worker queue is full.", error_message_len);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

cJSON *thread_cli_capabilities_json(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return nullptr;
    cJSON_AddNumberToObject(root, "max_line_length", THREAD_CLI_MAX_LINE_LENGTH - 1);
    cJSON_AddNumberToObject(root, "default_timeout_ms", THREAD_CLI_DEFAULT_TIMEOUT_MS);
    cJSON_AddNumberToObject(root, "max_timeout_ms", THREAD_CLI_MAX_TIMEOUT_MS);
    cJSON_AddBoolToObject(root, "streaming_events", true);
    cJSON_AddBoolToObject(root, "dangerous_requires_confirmation", true);
    cJSON_AddBoolToObject(root, "dangerous_requires_confirm_text", true);
    cJSON_AddStringToObject(root, "dangerous_confirm_text", "I understand");

    cJSON *classes = cJSON_AddObjectToObject(root, "classes");
    if (classes) {
        cJSON *read_only = cJSON_AddArrayToObject(classes, "read_only");
        cJSON *safe_write = cJSON_AddArrayToObject(classes, "safe_write");
        cJSON *network_mutation = cJSON_AddArrayToObject(classes, "network_mutation");
        cJSON *dangerous = cJSON_AddArrayToObject(classes, "dangerous");
        if (read_only) {
            const char *items[] = {
                "help", "state", "version", "extaddr", "eui64", "rloc16", "leaderdata",
                "router table", "child table", "neighbor table", "ipaddr", "ipmaddr",
                "netdata show", "netdata full", "dataset active", "dataset pending",
                "dataset tlvs", "channel", "panid", "extpanid", "networkname",
                "domainname", "mode", "counters", "history", "srp server state", "nat64 state"
            };
            for (const char *item : items) append_json_string(read_only, item);
        }
        if (safe_write) {
            const char *items[] = {
                "dataset init new", "dataset commit active", "ifconfig up", "ifconfig down",
                "thread start", "thread stop"
            };
            for (const char *item : items) append_json_string(safe_write, item);
        }
        if (network_mutation) {
            const char *items[] = {
                "commissioner start", "dataset networkkey <key>", "dataset pskc <pskc>",
                "dataset panid <panid>", "dataset channel <channel>",
                "dataset networkname <name>", "mode <mode>", "pollperiod <ms>",
                "udp open", "coap start", "netdata register", "prefix add", "route add"
            };
            for (const char *item : items) append_json_string(network_mutation, item);
        }
        if (dangerous) {
            const char *items[] = {
                "factoryreset", "reset", "dataset clear", "dataset set active <tlvs>",
                "dataset mgmtsetcommand active", "dataset mgmtsetcommand pending",
                "commissioner stop", "joiner start", "macfilter clear", "netdata register",
                "route add/remove", "prefix add/remove", "dns/srp destructive changes",
                "counters reset", "history clear"
            };
            for (const char *item : items) append_json_string(dangerous, item);
        }
    }
    return root;
}

cJSON *thread_cli_history_json(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return nullptr;
    cJSON *history = cJSON_AddArrayToObject(root, "history");
    if (!history) return root;
    for (const std::string &line : cli_history) {
        append_json_string(history, line);
    }
    return root;
}

esp_err_t thread_cli_cancel(const char *request_id) {
    if (!request_id || !session_mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(session_mutex, portMAX_DELAY);
    if (active_session && strcmp(active_session->request_id, request_id) == 0) {
        active_session->cancelled = true;
        if (active_session->done_sem) xSemaphoreGive(active_session->done_sem);
        xSemaphoreGive(session_mutex);
        return ESP_OK;
    }
    xSemaphoreGive(session_mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t thread_cli_service_init(void) {
    if (cli_initialized) return ESP_OK;

    otInstance *instance = esp_openthread_get_instance();
    if (!instance) return ESP_ERR_INVALID_STATE;

    if (!session_mutex) {
        session_mutex = xSemaphoreCreateMutex();
        if (!session_mutex) return ESP_ERR_NO_MEM;
    }
    if (!cli_queue) {
        cli_queue = xQueueCreate(THREAD_CLI_QUEUE_LENGTH, sizeof(ThreadCliWork));
        if (!cli_queue) return ESP_ERR_NO_MEM;
    }
    if (!cli_task_handle) {
        BaseType_t ok = xTaskCreate(thread_cli_worker,
                                    "thread_cli",
                                    THREAD_CLI_TASK_STACK_SIZE,
                                    nullptr,
                                    THREAD_CLI_TASK_PRIORITY,
                                    &cli_task_handle);
        if (ok != pdPASS) return ESP_ERR_NO_MEM;
    }

    otCliInit(instance, cli_output_callback, nullptr);
    cli_initialized = true;
    ESP_LOGI(TAG, "OpenThread CLI bridge initialized");
    return ESP_OK;
}
