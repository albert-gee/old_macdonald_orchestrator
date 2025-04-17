#ifndef MATTER_INTERFACE_H
#define MATTER_INTERFACE_H

#include <esp_err.h>
#include <esp_matter.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t matter_interface_init(esp_matter::event_callback_t handle_chip_device_event, intptr_t callback_arg);

esp_err_t matter_interface_controller_init(uint64_t node_id, uint64_t fabric_id, uint16_t listen_port);

#ifdef __cplusplus
}
#endif

#endif //MATTER_INTERFACE_H
