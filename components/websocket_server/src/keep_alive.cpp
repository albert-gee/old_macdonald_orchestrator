#include "keep_alive.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "portmacro.h"
#include "freertos/task.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"

static const char *TAG = "WSS_KEEP_ALIVE";

/**
 * @struct client_fd_action_t
 * Represents an action to be applied to client file descriptors.
 *
 * This structure enables the management and tracking of client states
 * by defining actions to be performed, along with additional
 * metadata, such as the file descriptor and the last interaction time.
 */
struct client_fd_action_t {
    /**
     * @enum Type
     * @brief Enum representing various types of client FD actions.
     *
     * This enum defines the types of actions that can be performed
     * on a client file descriptor, including adding, removing, updating,
     * marking as active, or stopping a task.
     *
     * @var Type::NO_CLIENT
     * Represents the absence of a client.
     *
     * @var Type::CLIENT_FD_ADD
     * Represents the action of adding a client file descriptor.
     *
     * @var Type::CLIENT_FD_REMOVE
     * Represents the action of removing a client file descriptor.
     *
     * @var Type::CLIENT_UPDATE
     * Represents the action of updating client information.
     *
     * @var Type::CLIENT_ACTIVE
     * Represents the action of marking a client as active.
     *
     * @var Type::STOP_TASK
     * Represents the action of stopping a task.
     */
    enum Type {
        NO_CLIENT = 0,
        CLIENT_FD_ADD,
        CLIENT_FD_REMOVE,
        CLIENT_UPDATE,
        CLIENT_ACTIVE,
        STOP_TASK
    } type;

    /**
     * @brief File descriptor representing a client connection.
     *
     * This integer variable is used as an identifier for a client's connection
     * within the WebSocket server's keep-alive mechanism. It indicates a specific
     * client session and is used in actions such as adding, removing, or
     * updating client records.
     *
     * A value of -1 typically signifies an uninitialized or invalid file descriptor.
     * The `fd` is associated with the `client_fd_action_t` structure to track
     * client activity and statuses.
     */
    int fd;
    /**
     * Tracks the last time a client was marked as active.
     *
     * This variable records the timestamp in milliseconds of the last observed activity from a client.
     * It is used to monitor client activity and determine their "alive" status in the keep-alive mechanism.
     *
     * The timestamp is updated each time the client's status is refreshed or when they perform an action
     * that signifies activity. It serves as a critical parameter in calculating the next expected check
     * for the client and enabling logic to remove inactive clients after a specified timeout period.
     */
    uint64_t last_seen;
};

/**
 * @struct wss_keep_alive_storage
 * @brief Stores data and configurations for the WebSocket Server (WSS) keep-alive mechanism.
 *
 * This structure is used to manage the keep-alive state for multiple WebSocket clients.
 * It includes configurations like the maximum number of clients, keep-alive intervals,
 * and callback functions to handle client status updates.
 *
 * @var max_clients
 * Maximum number of WebSocket clients supported by the keep-alive mechanism.
 *
 * @var check_client_alive_cb
 * Callback function to check if a specific WebSocket client is still alive.
 *
 * @var client_not_alive_cb
 * Callback function to handle actions when a client is determined to be not alive.
 *
 * @var keep_alive_period_ms
 * Time interval, in milliseconds, between successive keep-alive checks for each client.
 *
 * @var not_alive_after_ms
 * Time duration, in milliseconds, after which a client is marked as not alive if no response is received.
 *
 * @var user_ctx
 * User-defined context data, which can be used by the application.
 *
 * @var q
 * Queue handle used for inter-task actions and communication.
 *
 * @var clients
 * Dynamic array of type `client_fd_action_t`, which holds information about each client's state
 * and file descriptors. The size of the array is determined by `max_clients` and the queue size.
 */
struct wss_keep_alive_storage {
    /**
     * Maximum number of clients that can be managed within the WebSocket
     * keep-alive storage system.
     *
     * Represents the upper limit of the number of client FD actions
     * that the system can handle concurrently.
     */
    size_t max_clients;
    /**
     * Callback used to check if a specific WebSocket client is alive.
     *
     * The callback should determine if the given client, identified by its
     * file descriptor, is alive or responsive. It is invoked periodically
     * during the keep-alive task to ensure client connections are maintained.
     *
     * @param h A handle to the WebSocket keep-alive storage structure.
     * @param fd The file descriptor of the client to be checked.
     * @return True if the client is alive, false otherwise.
     */
    wss_check_client_alive_cb_t check_client_alive_cb;
    /**
     * A callback function of the type `wss_client_not_alive_cb_t` that is invoked when a client is determined to be
     * not alive.
     *
     * This callback is triggered by the keep-alive mechanism in scenarios where a client fails to respond
     * within the defined `not_alive_after_ms` interval. The function is responsible for handling the
     * disconnection or cleanup logic for the specified client.
     *
     * @note The function passed to this callback should accept two parameters:
     *       - A handle to the `wss_keep_alive_t` structure representing the keep-alive context.
     *       - An integer representing the file descriptor (fd) of the client determined to be not alive.
     *
     * @warning Ensure that the callback implementation does not block or introduce significant
     *          delays, as it operates within the keep-alive task context.
     */
    wss_client_not_alive_cb_t client_not_alive_cb;
    /**
     * @brief Specifies the interval in milliseconds for the periodic keep-alive checks.
     *
     * This variable determines the duration between consecutive checks to ensure that
     * connected clients are still active. It is used to calculate the next scheduled
     * keep-alive operation for active clients. Reducing this value increases the frequency
     * of checks, which may lead to faster detection of inactive clients but could also
     * result in higher processing overhead.
     *
     * The actual interval dynamically influences mechanisms such as calculating
     * the next keep-alive check and scheduling tasks accordingly.
     */
    size_t keep_alive_period_ms;
    /**
     * @brief Specifies the duration, in milliseconds, after which a client is considered not alive.
     *
     * This variable defines the timeout period for determining the liveness of a client. If the time
     * elapsed since the client's last activity (e.g., last seen) exceeds this value, the client is
     * treated as not alive.
     *
     * It is used within the keep-alive mechanism to periodically check the activity of connected
     * clients and trigger appropriate actions (e.g., marking the client as inactive or executing a
     * callback) when the client is deemed not alive.
     */
    size_t not_alive_after_ms;
    /**
     * @brief A user-defined context pointer.
     *
     * This variable allows the user to attach custom data or context to the
     * keep-alive system for use during runtime or callbacks.
     *
     * The `user_ctx` can be utilized to store application-specific
     * context or configuration that needs to be passed to the callbacks
     * or accessed from within the keep-alive system's tasks.
     *
     * Ownership and lifecycle management of the data pointed to by `user_ctx`
     * are the responsibility of the user. The system does not modify, free, or
     * validate the contents of this pointer.
     */
    void *user_ctx;
    /**
     * @brief Queue handle used for communication between the keep-alive task and other parts of the system.
     *
     * This queue is utilized to send and receive `client_fd_action_t` instances,
     * which represent actions related to client file descriptors, such as adding,
     * removing, updating clients, or stopping the task.
     *
     * It is created during initialization of the keep-alive service and deleted
     * when the service is stopped.
     */
    QueueHandle_t q;
    /**
     * @brief Array representing the actions for each client connected to the system.
     *
     * The `clients` array is part of the WebSocket server's keep-alive mechanism. Each entry
     * in the array corresponds to a specific client and holds information about the client's
     * current status, including its file descriptor, last seen timestamp, and the action to
     * be performed on the client.
     *
     * This array is utilized to track the active clients and manage their connections, ensuring
     * that clients are removed or updated based on their activity or lack thereof. The `type` field
     * in each element determines the specific action for a client.
     *
     * @note The size of the array depends on the `max_clients` field in the related
     *       `wss_keep_alive_storage` structure.
     *
     * @see client_fd_action_t
     * @see wss_keep_alive_storage
     */
    client_fd_action_t clients[];
};

/**
 * @brief Retrieves the current time in milliseconds.
 *
 * This function gets the current time from the system timer and converts it
 * from microseconds to milliseconds.
 *
 * @return The current time in milliseconds as a 64-bit integer.
 */
static int64_t get_current_time_ms() {
    return esp_timer_get_time() / 1000;
}

/**
 * @brief Default timeout duration (in milliseconds) for the keep-alive mechanism.
 *
 * This constant defines the default timeout value used in the keep-alive logic
 * to determine when the next check for client activity should be conducted.
 * It ensures that inactive clients are evaluated for disconnection or other action
 * within the specified time period.
 *
 * Use this value for initializing or managing client timeouts unless overridden
 * by specific settings in the application.
 */
static constexpr int64_t DEFAULT_KEEP_ALIVE_TIMEOUT_MS = 30000;
/**
 * Minimum interval in milliseconds for performing keep-alive checks.
 *
 * This constant defines the lower bound of time that must elapse between
 * successive keep-alive checks for active clients. It ensures that the
 * checks are not triggered too frequently, thereby preventing unnecessary
 * resource usage. This value also helps in maintaining a balance between
 * responsiveness and system efficiency during keep-alive operations.
 */
static constexpr int64_t MIN_KEEP_ALIVE_CHECK_MS = 1000;

/**
 * @brief Calculates the time until the next keep-alive check for active clients.
 *
 * This function iterates over the list of clients in the keep-alive storage
 * and computes the shortest time interval until a client requires the next
 * keep-alive check. Only clients marked as `CLIENT_ACTIVE` are considered.
 * If no active client has a shorter interval than the default keep-alive period,
 * the default timeout value is returned.
 *
 * @param h A pointer to the `wss_keep_alive_storage` structure containing client information.
 * @param current_time The current time in milliseconds, used for calculating the
 *                     time interval until the next keep-alive check.
 * @return The time in milliseconds until the next keep-alive check for the active clients.
 *         Returns `DEFAULT_KEEP_ALIVE_TIMEOUT_MS` if no client is active or if all
 *         active clients require checks after the default interval.
 */
static uint64_t calculate_next_check(const wss_keep_alive_storage *h, const int64_t current_time) {
    uint64_t next_check_time_ms = DEFAULT_KEEP_ALIVE_TIMEOUT_MS;

    for (size_t i = 0; i < h->max_clients; i++) {
        if (h->clients[i].type == client_fd_action_t::CLIENT_ACTIVE) {
            const uint64_t expected_next_check = h->clients[i].last_seen + h->keep_alive_period_ms;
            uint64_t time_until_check = expected_next_check - current_time;
            if (time_until_check < next_check_time_ms) {
                next_check_time_ms = time_until_check;
            }
        }
    }
    return next_check_time_ms;
}

/**
 * Calculates the amount of time in milliseconds until the next keep-alive check
 * should be performed based on the current time and keep-alive storage data.
 *
 * If the calculated delay is less than 0, it defaults to the minimum keep-alive
 * check interval defined by `MIN_KEEP_ALIVE_CHECK_MS`.
 *
 * @param h Pointer to the `wss_keep_alive_storage` structure containing
 *          configuration and client data for determining keep-alive timing.
 *
 * @return The time in milliseconds until the next keep-alive check should be
 *         performed.
 */
static uint64_t get_next_keep_alive_check(const wss_keep_alive_storage *h) {
    const int64_t now = get_current_time_ms();
    const uint64_t check_after_ms = calculate_next_check(h, now);

    return check_after_ms < MIN_KEEP_ALIVE_CHECK_MS ? MIN_KEEP_ALIVE_CHECK_MS : check_after_ms;
}

/**
 * @brief Registers a new client in the keep-alive storage.
 *
 * This function attempts to add a new client to the keep-alive storage. It iterates
 * through the storage to find an available slot for the new client. If a slot is
 * found, the client is marked as active, its file descriptor is stored, and the
 * client's last-seen time is updated to the current timestamp.
 *
 * @param h Pointer to the keep-alive storage structure.
 * @param client_fd File descriptor of the client to register.
 * @return true if the client is successfully registered.
 * @return false if no available slot is found or registration fails.
 */
static bool register_client(wss_keep_alive_storage *h, const int client_fd) {
    for (size_t i = 0; i < h->max_clients; i++) {
        if (h->clients[i].type == client_fd_action_t::NO_CLIENT) {
            h->clients[i].type = client_fd_action_t::CLIENT_ACTIVE;
            h->clients[i].fd = client_fd;
            h->clients[i].last_seen = get_current_time_ms();
            ESP_LOGI(TAG, "Client fd:%d added", client_fd);
            return true;
        }
    }
    ESP_LOGW(TAG, "Cannot add new client fd:%d", client_fd);
    return false;
}

/**
 * Unregisters a client from the `wss_keep_alive_storage` instance by removing
 * the client file descriptor (fd).
 *
 * This function iterates through the list of clients in the `wss_keep_alive_storage`
 * instance and searches for an active client (with the type `CLIENT_ACTIVE`)
 * that matches the given `client_fd`. If a match is found, the client's type
 * is set to `NO_CLIENT`, marking it as unregistered, and its file descriptor
 * is reset to -1.
 *
 * Logs the result of the operation. If the client is successfully removed,
 * an informational log message is printed. If the `client_fd` is invalid
 * or no matching client is found, a warning log message is printed.
 *
 * @param h Pointer to the `wss_keep_alive_storage` instance containing client
 *          information and status.
 * @param client_fd The file descriptor of the client to be unregistered.
 * @return `true` if the client was found and removed; `false` if the specified
 *         `client_fd` was not found or the client was already unregistered.
 */
static bool unregister_client(wss_keep_alive_storage *h, const int client_fd) {
    for (size_t i = 0; i < h->max_clients; i++) {
        if (h->clients[i].type == client_fd_action_t::CLIENT_ACTIVE && h->clients[i].fd == client_fd) {
            h->clients[i].type = client_fd_action_t::NO_CLIENT;
            h->clients[i].fd = -1;
            ESP_LOGI(TAG, "Client fd:%d removed", client_fd);
            return true;
        }
    }
    ESP_LOGW(TAG, "Attempted to remove invalid fd:%d", client_fd);
    return false;
}

/**
 * Updates the status of a client in the keep-alive storage by marking it as active
 * and updating its `last_seen` timestamp to the current time. It searches through
 * the list of clients in the specified storage for the given client file descriptor.
 *
 * @param h A pointer to the `wss_keep_alive_storage` structure that holds the client information.
 * @param client_fd The file descriptor of the client to be updated.
 * @return `true` if the client was successfully found and updated, `false` otherwise.
 */
static bool refresh_client_status(wss_keep_alive_storage *h, const int client_fd) {
    for (size_t i = 0; i < h->max_clients; i++) {
        if (h->clients[i].type == client_fd_action_t::CLIENT_ACTIVE && h->clients[i].fd == client_fd) {
            h->clients[i].last_seen = get_current_time_ms();
            ESP_LOGI(TAG, "Client fd:%d marked as active", client_fd);
            return true;
        }
    }
    ESP_LOGW(TAG, "Cannot find client fd:%d to update", client_fd);
    return false;
}

/**
 * @brief Task responsible for managing WebSocket client keep-alive functionality.
 *
 * This function runs as a FreeRTOS task and handles actions related to WebSocket clients
 * such as adding, removing, updating client statuses, and monitoring their activity.
 * It operates based on messages received through a task queue and periodically checks
 * the status of active client connections.
 *
 * @param arg Pointer to the `wss_keep_alive_storage` structure containing configuration
 *            and state information for the keep-alive mechanism.
 *
 * The task performs the following operations:
 * - Adds, removes, or updates clients based on messages received from the queue.
 * - Periodically checks for inactive clients by comparing the current time with their
 *   last-seen timestamp.
 * - Invokes user-defined callbacks for clients no longer considered alive or for
 *   custom checks on active clients.
 * - Terminates and cleans up resources when a STOP_TASK action is received.
 *
 * Key behaviors:
 * - If no action is received before timeout, it iterates through all clients
 *   and checks for inactivity.
 * - Executes user-provided callbacks to handle client-specific actions such as
 *   notifying about inactive clients or performing custom checks.
 *
 * Task cleanup:
 * - Deletes the internal queue.
 * - Frees the storage memory for the keep-alive data structure.
 * - Deletes itself upon termination.
 */
static void keep_alive_task(void *arg) {
    auto *h = static_cast<wss_keep_alive_storage *>(arg);
    bool run_task = true;

    while (run_task) {
        const uint64_t timeout_ms = get_next_keep_alive_check(h);
        client_fd_action_t action = {};

        if (xQueueReceive(h->q, &action, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
            switch (action.type) {
                case client_fd_action_t::CLIENT_FD_ADD:
                    register_client(h, action.fd);
                    break;
                case client_fd_action_t::CLIENT_FD_REMOVE:
                    unregister_client(h, action.fd);
                    break;
                case client_fd_action_t::CLIENT_UPDATE:
                    refresh_client_status(h, action.fd);
                    break;
                case client_fd_action_t::STOP_TASK:
                    run_task = false;
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown action type:%d", action.type);
                    break;
            }
        } else {
            const uint64_t now = get_current_time_ms();
            for (size_t i = 0; i < h->max_clients; i++) {
                if (h->clients[i].type == client_fd_action_t::CLIENT_ACTIVE) {
                    if ((now - h->clients[i].last_seen) > h->not_alive_after_ms) {
                        ESP_LOGW(TAG, "Client fd:%d not alive", h->clients[i].fd);
                        if (h->client_not_alive_cb) {
                            h->client_not_alive_cb(h, h->clients[i].fd);
                        }
                        h->clients[i].type = client_fd_action_t::NO_CLIENT;
                    } else if (h->check_client_alive_cb) {
                        h->check_client_alive_cb(h, h->clients[i].fd);
                    }
                }
            }
        }
    }

    vQueueDelete(h->q);
    free(h);
    vTaskDelete(nullptr);
}

wss_keep_alive_t wss_keep_alive_start(wss_keep_alive_config_t *config) {
    if (config->task_stack_size < 4096) {
        ESP_LOGW(TAG, "Increasing task stack size to 8192");
        config->task_stack_size = 8192;
    }

    const size_t queue_size = config->max_clients / 2;
    const size_t client_list_size = config->max_clients + queue_size;

    auto *h = static_cast<wss_keep_alive_storage *>(
        calloc(1, sizeof(wss_keep_alive_storage) + client_list_size * sizeof(client_fd_action_t))
    );
    if (!h) return nullptr;

    h->check_client_alive_cb = config->check_client_alive_cb;
    h->client_not_alive_cb = config->client_not_alive_cb;
    h->max_clients = config->max_clients;
    h->keep_alive_period_ms = config->keep_alive_period_ms;
    h->not_alive_after_ms = config->not_alive_after_ms;
    h->user_ctx = config->user_ctx;
    h->q = xQueueCreate(queue_size, sizeof(client_fd_action_t));

    if (!h->q || xTaskCreate(keep_alive_task, "keep_alive_task", config->task_stack_size, h, config->task_prio,
                             nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start keep_alive task");
        if (h->q) vQueueDelete(h->q);
        free(h);
        return nullptr;
    }

    return h;
}

void wss_keep_alive_stop(const wss_keep_alive_t h) {
    constexpr client_fd_action_t stop = {.type = client_fd_action_t::STOP_TASK, .fd = -1, .last_seen = 0};
    xQueueSendToBack(h->q, &stop, 0);
}

esp_err_t wss_keep_alive_add_client(const wss_keep_alive_t h, const int fd) {
    const client_fd_action_t action = {.type = client_fd_action_t::CLIENT_FD_ADD, .fd = fd, .last_seen = 0};
    return xQueueSendToBack(h->q, &action, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t wss_keep_alive_remove_client(const wss_keep_alive_t h, const int fd) {
    const client_fd_action_t action = {.type = client_fd_action_t::CLIENT_FD_REMOVE, .fd = fd, .last_seen = 0};
    return xQueueSendToBack(h->q, &action, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t wss_keep_alive_client_is_active(const wss_keep_alive_t h, const int fd) {
    const client_fd_action_t action = {.type = client_fd_action_t::CLIENT_UPDATE, .fd = fd, .last_seen = 0};
    return xQueueSendToBack(h->q, &action, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

void wss_keep_alive_set_user_ctx(const wss_keep_alive_t h, void *ctx) {
    h->user_ctx = ctx;
}

void *wss_keep_alive_get_user_ctx(const wss_keep_alive_t h) {
    return h->user_ctx;
}
