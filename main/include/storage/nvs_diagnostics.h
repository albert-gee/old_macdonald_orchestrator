#ifndef NVS_DIAGNOSTICS_H
#define NVS_DIAGNOSTICS_H

#include <cJSON.h>
#include <esp_err.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *label;
    size_t used_entries;
    size_t free_entries;
    size_t total_entries;
    size_t namespace_count;
    esp_err_t err;
} nvs_diag_t;

esp_err_t init_nvs_partition_or_recover(const char *label);
esp_err_t read_nvs_diag(const char *label, nvs_diag_t *diag);
void log_nvs_stats(const char *label);
cJSON *nvs_diagnostics_to_json(void);

#define OT_NVS_PARTITION_NAME "ot_nvs"
#define OT_NVS_NAMESPACE "openthread"

#ifdef __cplusplus
}
#endif

#endif
