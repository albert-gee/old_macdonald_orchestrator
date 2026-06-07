#ifndef MATTER_DISCOVERY_H
#define MATTER_DISCOVERY_H

#include <app/ConcreteAttributePath.h>
#include <lib/core/CHIPError.h>
#include <lib/core/TLVReader.h>
#include <esp_err.h>
#include <stdint.h>

esp_err_t matter_discovery_refresh_device(const char *device_id);
void matter_discovery_handle_attribute_report(uint64_t node_id,
                                              const chip::app::ConcreteDataAttributePath &path,
                                              chip::TLV::TLVReader *data);

#endif
