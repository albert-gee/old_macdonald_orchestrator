#include "websocket_server.h"
#include <esp_log.h>
#include <esp_https_server.h>
#include <esp_timer.h>
#include <unistd.h>
#include "keep_alive.h"

constexpr int MAX_CLIENTS = 10;
constexpr char WEBSOCKET_URI[] = "/ws";

static httpd_handle_t server = nullptr;
static ws_connection_handler_t connection_handler = nullptr;
static ws_inbound_message_handler_t message_handler = nullptr;
static wss_keep_alive_t keep_alive = nullptr;

extern const char servercert_pem_start[] asm("_binary_servercert_pem_start");
extern const char servercert_pem_end[] asm("_binary_servercert_pem_end");
extern const char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
extern const char prvtkey_pem_end[] asm("_binary_prvtkey_pem_end");

struct async_send_arg {
    httpd_handle_t httpd_handle;
    int client_fd;
    char *message;
};

static void free_async_send_arg(async_send_arg *arg) {
    free(arg->message);
    free(arg);
}

static void async_send_task(void *arg) {
    auto *send_arg = static_cast<async_send_arg *>(arg);
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = reinterpret_cast<uint8_t *>(send_arg->message),
        .len = strlen(send_arg->message)
    };
    httpd_ws_send_frame_async(send_arg->httpd_handle, send_arg->client_fd, &frame);
    free_async_send_arg(send_arg);
}

static httpd_ssl_config_t configure_ssl() {
    httpd_ssl_config_t ssl_config = HTTPD_SSL_CONFIG_DEFAULT();
    ssl_config.httpd.global_user_ctx = keep_alive;
    ssl_config.httpd.close_fn = [](httpd_handle_t, int fd) {
        wss_keep_alive_remove_client(keep_alive, fd);
        close(fd);
    };

    ssl_config.servercert = reinterpret_cast<const uint8_t *>(servercert_pem_start);
    ssl_config.servercert_len = servercert_pem_end - servercert_pem_start;
    ssl_config.prvtkey_pem = reinterpret_cast<const uint8_t *>(prvtkey_pem_start);
    ssl_config.prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;

    return ssl_config;
}

static void handle_frame_type(const httpd_ws_frame_t &frame, const int fd) {
    switch (frame.type) {
        case HTTPD_WS_TYPE_TEXT:
            if (message_handler) message_handler(reinterpret_cast<const char *>(frame.payload));
            break;
        case HTTPD_WS_TYPE_PONG:
            wss_keep_alive_client_is_active(keep_alive, fd);
            break;
        case HTTPD_WS_TYPE_CLOSE:
            wss_keep_alive_remove_client(keep_alive, fd);
            break;
        default:
            break;
    }
}

static esp_err_t receive_ws_frame(httpd_req_t *req, httpd_ws_frame_t &frame) {
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK || frame.len == 0) return ret;

    auto *buf = static_cast<uint8_t *>(calloc(1, frame.len + 1));
    if (!buf) return ESP_ERR_NO_MEM;

    frame.payload = buf;
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret == ESP_OK) handle_frame_type(frame, httpd_req_to_sockfd(req));
    free(buf);
    return ret;
}

static esp_err_t ws_handler(httpd_req_t *req) {
    const int fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        wss_keep_alive_add_client(keep_alive, fd);
        if (connection_handler) connection_handler(fd);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {};
    return receive_ws_frame(req, frame);
}

esp_err_t websocket_server_start(const ws_connection_handler_t connection_handler_fun, const ws_inbound_message_handler_t message_handler_fun) {
    if (server) return ESP_FAIL;

    connection_handler = connection_handler_fun;
    message_handler = message_handler_fun;

    wss_keep_alive_config_t keep_alive_config = KEEP_ALIVE_CONFIG_DEFAULT();
    keep_alive_config.client_not_alive_cb = [](wss_keep_alive_t h, int fd) {
        httpd_sess_trigger_close(wss_keep_alive_get_user_ctx(h), fd);
        return true;
    };
    keep_alive_config.check_client_alive_cb = [](wss_keep_alive_t h, int fd) {
        auto hd = wss_keep_alive_get_user_ctx(h);
        httpd_ws_frame_t ping = {.type = HTTPD_WS_TYPE_PING, .payload = nullptr, .len = 0};
        httpd_ws_send_frame_async(hd, fd, &ping);
        return true;
    };
    keep_alive = wss_keep_alive_start(&keep_alive_config);

    httpd_ssl_config_t ssl_config = configure_ssl();

    esp_err_t ret = httpd_ssl_start(&server, &ssl_config);
    if (ret != ESP_OK) return ret;

    static constexpr httpd_uri_t ws_uri = {
        .uri = WEBSOCKET_URI,
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
        .handle_ws_control_frames = true
    };

    httpd_register_uri_handler(server, &ws_uri);
    wss_keep_alive_set_user_ctx(keep_alive, server);
    return ESP_OK;
}

esp_err_t websocket_server_stop() {
    if (!server) return ESP_FAIL;
    wss_keep_alive_stop(keep_alive);
    keep_alive = nullptr;
    esp_err_t ret = httpd_ssl_stop(server);
    server = nullptr;
    return ret;
}

esp_err_t websocket_send_message_to_client(const int fd, const char *message) {
    if (!server || !message) return ESP_ERR_INVALID_ARG;

    auto *arg = static_cast<async_send_arg *>(malloc(sizeof(async_send_arg)));
    arg->httpd_handle = server;
    arg->client_fd = fd;
    arg->message = strdup(message);

    return httpd_queue_work(server, async_send_task, arg);
}

esp_err_t websocket_broadcast_message(const char *message) {
    if (!server || !message) return ESP_ERR_INVALID_ARG;

    int fds[MAX_CLIENTS];
    size_t count = MAX_CLIENTS;
    if (httpd_get_client_list(server, &count, fds) != ESP_OK) return ESP_FAIL;

    for (size_t i = 0; i < count; ++i) {
        if (httpd_ws_get_fd_info(server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            websocket_send_message_to_client(fds[i], message);
        }
    }
    return ESP_OK;
}
