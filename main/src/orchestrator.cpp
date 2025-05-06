#include "event_handlers/chip_event_handler.h"
#include "event_handlers/thread_event_handler.h"
#include "event_handlers/wifi_event_handler.h"
#include "thread_interface.h"
#include "matter_interface.h"
#include "wifi_interface.h"

#include <nvs_flash.h>
#include <esp_netif.h>

static const char *TAG = "ORCHESTRATOR";

extern "C" void app_main() {

    // Initialize ESP NVS layer
    ESP_LOGI(TAG, "Initializing NVS Flash");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Create the default event loop
    ESP_LOGI(TAG, "Creating default event loop");
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
        return;
    }

    // Initialize Netif
    ESP_LOGI(TAG, "Initializing esp_netif");
    err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize esp_netif: %s", esp_err_to_name(err));
        return;
    }

    // Initialize Thread Interface
#if CONFIG_OPENTHREAD_ENABLED
    err = thread_interface_init(handle_thread_event);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Thread stack: %s", esp_err_to_name(err));
        return;
    }
#endif // CONFIG_OPENTHREAD_ENABLED

    // Initialize Wi-Fi Interface
#if CONFIG_ENABLE_WIFI_STATION
    err = wifi_interface_init(handle_wifi_event);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi interface: %s", esp_err_to_name(err));
        return;
    }
#endif

    // Initialize Matter Interface
    err = matter_interface_init(handle_chip_device_event, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Matter interface: %s", esp_err_to_name(err));
        return;
    }

    // Start Wi-Fi
    // This stape should go after the Matter stack initialization because Matter disables Wi-Fi AP mode
    err = wifi_interface_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi AP+STA: %s", esp_err_to_name(err));
    }
}
