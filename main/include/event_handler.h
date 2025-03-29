#ifndef CHIP_EVENT_HANDLER_H
#define CHIP_EVENT_HANDLER_H

#include <esp_matter.h>

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief Handles Wi-Fi events.
     *
     * @param arg A custom argument passed with the event (optional).
     * @param event_base The base of the event (e.g., WIFI_EVENT).
     * @param event_id The specific event ID (e.g., ESP_EVENT_ANY_ID).
     * @param event_data Pointer to the event data (optional).
     */
    void handle_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

    /**
     * @brief Handles OpenThread events.
     *
     * @param arg A custom argument passed with the event (optional).
     * @param event_base The base of the event (e.g., OPENTHREAD_EVENT).
     * @param event_id The specific event ID (e.g., OPENTHREAD_EVENT_DATASET_CHANGED).
     * @param event_data Pointer to the event data (optional).
     */
    void handle_thread_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

    /**
     * @brief Handles Chip device events.
     *
     * @param event The incoming ChipDeviceEvent object.
     * @param arg A custom argument passed with the event (optional).
     */
    void handle_chip_device_event(const ChipDeviceEvent *event, intptr_t arg);

#ifdef __cplusplus
}
#endif

#endif // CHIP_EVENT_HANDLER_H
