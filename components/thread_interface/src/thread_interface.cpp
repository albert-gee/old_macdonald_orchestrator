#include "thread_interface.h"

#include <esp_log.h>
#include <esp_openthread.h>
#include <esp_openthread_netif_glue.h>
#include <esp_ot_config.h>

static const char *TAG = "THREAD_INTERFACE";

// Worker task to manage OpenThread main loop
static void ot_task_worker(void * context)
{
    // Launch the OpenThread main loop
    // The function will not return unless an error occurs during OpenThread stack execution
    esp_openthread_launch_mainloop();

    // Clean up OpenThread resources
    esp_openthread_deinit();
    esp_openthread_netif_glue_deinit();

    // Delete the task
    vTaskDelete(nullptr);
}

esp_err_t thread_interface_init()
{
    esp_err_t err = ESP_OK;

    // Configure and create a new network interface for OpenThread
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *netif = esp_netif_new(&netif_config);
    if (netif == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create OpenThread network interface");
        return ESP_ERR_NO_MEM;
    }

    // Attach OpenThread network interface to ESP-NETIF
    esp_openthread_platform_config_t ot_platform_config = ESP_OPENTHREAD_DEFAULT_CONFIG();
    err = esp_netif_attach(netif, esp_openthread_netif_glue_init(&ot_platform_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to attach OpenThread network interface");
        esp_netif_destroy(netif);
        return err;
    }

    // Initialize the full OpenThread stack
    err = esp_openthread_init(&ot_platform_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize OpenThread stack");
        return err;
    }

    // Create a worker task for the OpenThread main loop
    xTaskCreate(ot_task_worker, "ot_task", CONFIG_THREAD_TASK_STACK_SIZE,
                xTaskGetCurrentTaskHandle(), 5, NULL);

    return err;
}
