#ifndef CHIP_EVENT_HANDLER_H
#define CHIP_EVENT_HANDLER_H

#include <esp_matter.h>

#ifdef __cplusplus
extern "C" {
#endif

void handle_chip_device_event(const ChipDeviceEvent *event, intptr_t arg);

#ifdef __cplusplus
}
#endif


#endif //CHIP_EVENT_HANDLER_H
