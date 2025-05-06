#ifndef CHIP_EVENT_HANDLER_H
#define CHIP_EVENT_HANDLER_H

#include <esp_matter.h>

#ifdef __cplusplus
extern "C" {
#endif

void handle_chip_device_event(const ChipDeviceEvent *event, intptr_t arg);

void attribute_data_report_callback(uint64_t remote_node_id,
                                  const chip::app::ConcreteDataAttributePath &path,
                                  TLVReader *data);

void subscribe_done_callback(uint64_t remote_node_id,
                             uint32_t subscription_id);

#ifdef __cplusplus
}
#endif

#endif //CHIP_EVENT_HANDLER_H
