#include "websocket_client.h"
#include <esp_log.h>
#include <portmacro.h>
#include <cstdlib>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <sys/queue.h>

static const char *TAG = "WS_CLIENT";

client_list clients = LIST_HEAD_INITIALIZER(clients);
static StaticSemaphore_t mutex_buffer;
SemaphoreHandle_t client_mutex; // No 'static' since it's extern in the header

void websocket_client_init() {
    client_mutex = xSemaphoreCreateMutexStatic(&mutex_buffer);
    if (client_mutex == nullptr) {
        ESP_LOGE(TAG, "Failed to create client management mutex");
        return;
    }
    LIST_INIT(&clients);
    ESP_LOGI(TAG, "Client management system initialized");
}
void websocket_client_add(const int fd) {
    auto *client = static_cast<websocket_client_t *>(malloc(sizeof(websocket_client_t)));
    if (!client) {
        ESP_LOGE(TAG, "Failed to allocate memory for client");
        return;
    }

    client->fd = fd;
    client->authenticated = false;
    client->connect_time = xTaskGetTickCount();

    if (xSemaphoreTake(client_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex in websocket_client_add");
        free(client);
        return;
    }
    LIST_INSERT_HEAD(&clients, client, entries);
    xSemaphoreGive(client_mutex);

    ESP_LOGI(TAG, "Client added, FD: %d", fd);
}

void websocket_client_remove(int fd) {
    if (xSemaphoreTake(client_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex in websocket_client_remove");
        return;
    }

    websocket_client_t *client;
    LIST_FOREACH(client, &clients, entries) {
        if (client->fd == fd) {
            LIST_REMOVE(client, entries);
            free(client);
            ESP_LOGI(TAG, "Client removed, FD: %d", fd);
            break;
        }
    }

    xSemaphoreGive(client_mutex);
}

websocket_client_t* websocket_client_find(const int fd) {
    ESP_LOGI(TAG, "Finding client for FD: %d", fd);

    websocket_client_t *client = nullptr;

    if (xSemaphoreTake(client_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex in websocket_client_find");
        return nullptr;
    }

    ESP_LOGI(TAG, "Searching for client with FD: %d", fd);
    LIST_FOREACH(client, &clients, entries) {
        if (client->fd == fd) {
            ESP_LOGI(TAG, "Client found, FD: %d", fd);
            break;
        }
    }
    ESP_LOGI(TAG, "Client search completed");

    xSemaphoreGive(client_mutex);
    ESP_LOGI(TAG, "Returning client for FD: %d", fd);

    return client;
}

bool websocket_client_is_authenticated(const int fd) {
    ESP_LOGI(TAG, "Checking authentication status for FD: %d", fd);

    const websocket_client_t *client = websocket_client_find(fd);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Client not found for FD: %d", fd);
        return false;
    }

    bool ret = client->authenticated;
    ESP_LOGI(TAG, "Client authentication status: %s", ret ? "Authenticated" : "Not Authenticated");
    return ret;
}

void websocket_client_authenticate(int fd) {
    if (xSemaphoreTake(client_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex in websocket_client_authenticate");
        return;
    }

    websocket_client_t *client;
    LIST_FOREACH(client, &clients, entries) {
        if (client->fd == fd) {
            client->authenticated = true;
            ESP_LOGI(TAG, "Client authenticated, FD: %d", fd);
            break;
        }
    }

    xSemaphoreGive(client_mutex);
}