#ifndef KEEP_ALIVE_H
#define KEEP_ALIVE_H

#include "esp_err.h"
#include "stdint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct wss_keep_alive_storage
 * @brief Represents the internal structure for managing WebSocket keep-alive functionality.
 *
 * The `wss_keep_alive_storage` structure is responsible for managing the storage and operations
 * related to WebSocket client keep-alive mechanisms. It provides support for tracking connected
 * clients, monitoring their activity, and ensuring timely removal of inactive or unresponsive clients.
 *
 * This structure includes capabilities for:
 * - Keeping track of active clients and their last activity timestamps.
 * - Allowing custom callbacks for checking if a client is alive and handling cases where a client is not alive.
 * - Managing configurable keep-alive and timeout parameters for client activity checks.
 *
 * The structure uses an internal queue mechanism to handle client registration, removal, and
 * updates efficiently.
 */
struct wss_keep_alive_storage;
typedef wss_keep_alive_storage *wss_keep_alive_t;

typedef bool (*wss_check_client_alive_cb_t)(wss_keep_alive_t h, int fd);

typedef bool (*wss_client_not_alive_cb_t)(wss_keep_alive_t h, int fd);

typedef struct {
    /**
     * @var max_clients
     * @brief Specifies the maximum number of clients the WebSocket server can manage for keep-alive functionality.
     *
     * The `max_clients` parameter defines an upper limit on the number of clients that can be concurrently
     * registered and managed for activity monitoring. This value directly impacts the allocation of internal
     * structures used for tracking client state and communication queues.
     *
     * Key details:
     * - If the number of connected clients exceeds this limit, new connections may be rejected, or existing ones
     *   may be dropped depending on the implementation logic.
     * - This parameter influences the size of allocated queues and memory used for client state tracking.
     */
    size_t max_clients;
    /**
     * @var task_stack_size
     * @brief Defines the stack size allocated for the keep-alive task.
     *
     * The `task_stack_size` variable specifies the amount of stack memory
     * (in bytes) to be allocated for the task responsible for handling
     * WebSocket keep-alive operations. It is a configurable parameter
     * that directly affects the task's memory footprint and should be
     * set according to the complexity of the task and the available system
     * resources.
     *
     * - A minimum stack size of 2048 bytes is enforced by default. If a
     *   smaller value is provided, it will be increased to 4096 bytes
     *   automatically to ensure proper functioning.
     * - In scenarios where memory constraints are not an issue, a higher
     *   stack size can be set to accommodate advanced operations or
     *   additional tasks performed by the keep-alive task.
     */
    size_t task_stack_size;
    /**
     * @var task_prio
     * @brief Specifies the priority of the task responsible for handling keep-alive functionality in the WebSocket server.
     *
     * The `task_prio` variable determines the priority level at which the keep-alive task operates
     * within the FreeRTOS scheduling system. This priority level is used when creating the task
     * to balance its execution against other tasks in the application.
     *
     * Proper configuration of `task_prio` ensures that the keep-alive task efficiently monitors
     * client activity without interfering with the operation of higher-priority tasks or causing
     * resource contention.
     */
    size_t task_prio;
    /**
     * @brief Defines the periodic interval (in milliseconds) for executing WebSocket keep-alive checks.
     *
     * The `keep_alive_period_ms` variable specifies the duration, in milliseconds, between consecutive
     * checks to verify the activity or responsiveness of connected WebSocket clients. This interval
     * determines how often the keep-alive mechanism runs to assess the state of clients, ensuring timely
     * detection of potentially unresponsive clients.
     *
     * A lower value for this parameter results in more frequent checks, which can detect issues more
     * quickly but may impose a higher processing overhead. Conversely, a higher value reduces overhead
     * but may delay detection of inactive clients.
     *
     * This value is configured via the `wss_keep_alive_config_t` structure and used internally to
     * control the keep-alive task's sleep or delay duration between each execution cycle.
     */
    size_t keep_alive_period_ms;
    /**
     * @var not_alive_after_ms
     * @brief Specifies the timeout duration (in milliseconds) for determining client inactivity.
     *
     * The `not_alive_after_ms` variable defines the maximum time allowed since the last keep-alive
     * signal or activity from a WebSocket client before it is considered inactive or unresponsive.
     * If a client surpasses this timeout, the keep-alive mechanism will trigger the configured
     * `client_not_alive_cb` callback to handle the unresponsive client.
     *
     * This parameter is configurable and plays a critical role in maintaining the responsiveness
     * and resource utilization of the WebSocket server.
     */
    size_t not_alive_after_ms;
    /**
     * @typedef wss_check_client_alive_cb_t
     * @brief Callback function type for checking the liveliness of a WebSocket client.
     *
     * The `wss_check_client_alive_cb_t` defines the signature for a callback function
     * that determines whether a WebSocket client is still active or alive.
     * It is used by the WebSocket server's keep-alive mechanism to periodically
     * verify the activity status of connected clients.
     *
     * The callback takes in the handle to the keep-alive storage (`wss_keep_alive_t`)
     * and a file descriptor (`fd`) identifying the client connection. It returns a boolean
     * value indicating the client's status:
     * - `true`: Client is alive.
     * - `false`: Client is not alive.
     *
     * This callback allows customization of liveliness checks, enabling
     * application-specific logic to determine client activity.
     */
    wss_check_client_alive_cb_t check_client_alive_cb;
    /**
     * @var client_not_alive_cb
     * @brief Callback function invoked when a WebSocket client is detected as not alive.
     *
     * The `client_not_alive_cb` function pointer is used to define a custom callback that is triggered
     * when a WebSocket client is determined to be unresponsive or inactive. This callback can be used
     * to perform cleanup actions or other necessary handling for the disconnected or inactive client.
     *
     * Key details:
     * - Parameters:
     *   - A handle to the WebSocket keep-alive context (`wss_keep_alive_t`).
     *   - An integer file descriptor (`fd`) representing the client detected as not alive.
     * - The callback should return a boolean value indicating if the function executed successfully.
     *
     * This callback is configurable via `wss_keep_alive_config_t` and is utilized internally in the
     * WebSocket keep-alive mechanism for managing inactive clients.
     */
    wss_client_not_alive_cb_t client_not_alive_cb;
    /**
     * @var user_ctx
     * @brief User-defined context pointer utilized for custom application logic.
     *
     * The `user_ctx` variable allows the user to associate an arbitrary context
     * with the WebSocket keep-alive configuration. This pointer can be used to
     * store application-specific data or structures that may be required during
     * runtime operations of the WebSocket server or keep-alive task processes.
     *
     * Typical use cases include:
     * - Providing additional data or state to callback functions such as
     *   `check_client_alive_cb` or `client_not_alive_cb`.
     * - Passing shared resources or handles between different parts of the
     *   application using the WebSocket keep-alive module.
     *
     * This variable is initialized to `NULL` by default but can be set through
     * the `wss_keep_alive_set_user_ctx` function or directly via the configuration
     * structure prior to starting the WebSocket keep-alive.
     */
    void *user_ctx;
} wss_keep_alive_config_t;

#define KEEP_ALIVE_CONFIG_DEFAULT() { \
    .max_clients = 10, \
    .task_stack_size = 2048, \
    .task_prio = tskIDLE_PRIORITY + 1, \
    .keep_alive_period_ms = 5000, \
    .not_alive_after_ms = 10000, \
    .check_client_alive_cb = NULL, \
    .client_not_alive_cb = NULL, \
    .user_ctx = NULL \
    }

/**
 * Initializes and starts the WebSocket Secure (WSS) keep-alive mechanism.
 *
 * @param config A pointer to the configuration structure `wss_keep_alive_config_t` that defines
 *               the parameters for the keep-alive mechanism, such as the maximum number of clients,
 *               task configuration, callback functions for checking client status, and timeout values.
 *
 * @return A handle to the WSS keep-alive storage instance (`wss_keep_alive_t`) if successful,
 *         or `nullptr` if initialization or resource allocation fails.
 */
wss_keep_alive_t wss_keep_alive_start(wss_keep_alive_config_t *config);

/**
 * Stops the keep-alive task associated with the specified WebSocket server.
 * This function sends a signal to the keep-alive queue to stop its processing.
 *
 * @param h Handle to the keep-alive storage structure. Must be a valid pointer
 *          to a `wss_keep_alive_t` object representing the keep-alive context.
 */
void wss_keep_alive_stop(wss_keep_alive_t h);

/**
 * Adds a new client to the WebSocket server's keep-alive manager.
 *
 * @param h The `wss_keep_alive_t` handle representing the keep-alive manager instance.
 * @param fd The file descriptor (socket) associated with the client to be added.
 * @return `ESP_OK` if the client is successfully added to the queue, otherwise `ESP_FAIL`.
 */
esp_err_t wss_keep_alive_add_client(wss_keep_alive_t h, int fd);

/**
 * @brief Removes a client from the WebSocket server's keep-alive tracking.
 *
 * This function sends a request to the internal keep-alive mechanism to remove
 * the specified client associated with the given file descriptor. The removal
 * action is enqueued for processing and confirms disconnection of the client
 * from the keep-alive monitoring.
 *
 * @param h Handle to the WebSocket server keep-alive context.
 * @param fd File descriptor of the client to be removed from the keep-alive tracking.
 * @return ESP_OK if the removal request is successfully enqueued, ESP_FAIL otherwise.
 */
esp_err_t wss_keep_alive_remove_client(wss_keep_alive_t h, int fd);

/**
 * Updates the last active status of a client in the WebSocket server's
 * keep-alive mechanism.
 *
 * This function informs the keep-alive system that a specific client,
 * identified by its file descriptor, has performed an activity. It ensures
 * that the client's connection is marked as active and prevents it from being
 * flagged as inactive until the next activity check.
 *
 * @param h A pointer to the keep-alive storage (wss_keep_alive_t).
 *          Represents the context of the WebSocket server's keep-alive system.
 * @param fd The file descriptor of the client whose activity status
 *           is being updated.
 *
 * @return ESP_OK if the client activity was successfully recorded,
 *         ESP_FAIL if the operation could not be completed due to an error
 *         (e.g., queue is full).
 */
esp_err_t wss_keep_alive_client_is_active(wss_keep_alive_t h, int fd);

/**
 * Sets the user context in a given WebSocket keep-alive handle.
 *
 * This function allows the user to associate a custom context with the
 * specified keep-alive handle, making it accessible for various operations
 * during the lifetime of the handle.
 *
 * @param h The keep-alive handle where the user context will be set.
 * @param ctx A pointer to the user-defined context to set in the keep-alive handle.
 */
void wss_keep_alive_set_user_ctx(wss_keep_alive_t h, void *ctx);

/**
 * Retrieves the user-defined context associated with the WebSocket server's
 * keep-alive instance.
 *
 * @param h A handle to the WebSocket server's keep-alive instance.
 * @return A pointer to the user-defined context associated with the instance.
 *         If no context is set, returns nullptr.
 */
void *wss_keep_alive_get_user_ctx(wss_keep_alive_t h);

#ifdef __cplusplus
}
#endif

#endif // KEEP_ALIVE_H
