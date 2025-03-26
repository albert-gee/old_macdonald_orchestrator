#ifndef CHIP_EVENT_HANDLER_H
#define CHIP_EVENT_HANDLER_H

#include <esp_matter.h>

#ifdef __cplusplus
extern "C" {
#endif

    void handle_thread_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    /**
     * @brief Handles Chip device events, including OpenThread and Wi-Fi events.
     *
     * @param event The incoming ChipDeviceEvent object.
     * @param arg A custom argument passed with the event (optional).
     */
    void handle_chip_device_event(const ChipDeviceEvent *event, intptr_t arg);

#ifdef __cplusplus
}
#endif

#endif // CHIP_EVENT_HANDLER_H
