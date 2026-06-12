#include "matter_controller.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_client.h>
#include <esp_netif.h>
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <portmacro.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_controller_cluster_command.h>
#include <esp_matter_controller_read_command.h>
#include <esp_matter_controller_subscribe_command.h>
#include <esp_matter_controller_utils.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <credentials/FabricTable.h>
#include <inet/IPAddress.h>
#include <lib/address_resolve/AddressResolve_DefaultImpl.h>
#include <lib/dnssd/Types.h>
#include <lib/support/SafeString.h>
#include <openthread/ip6.h>
#include <openthread/thread.h>
#include <openthread/thread_ftd.h>
#include <platform/PlatformManager.h>

static const char *TAG = "MATTER_UTIL";
static constexpr TickType_t MATTER_CONTROLLER_LOCK_TIMEOUT = pdMS_TO_TICKS(15000);
static constexpr uint16_t DEFAULT_MATTER_OPERATIONAL_PORT = 5540;
static constexpr int64_t OPERATIONAL_ADDRESS_HINT_WINDOW_US = 180LL * 1000LL * 1000LL;
static constexpr uint32_t OPERATIONAL_ADDRESS_HINT_TASK_STACK = 4096;
static constexpr TickType_t OPERATIONAL_ADDRESS_HINT_INTERVAL = pdMS_TO_TICKS(250);

static esp_matter::controller::attribute_report_cb_t attribute_report_cb = nullptr;
static esp_matter::controller::subscribe_done_cb_t subscribe_done_cb = nullptr;

struct WifiOperationalAddressHint {
    bool active;
    bool task_running;
    uint64_t node_id;
    uint16_t port;
    char ip[48];
    int64_t deadline_us;
    bool scan_thread_address_cache;
};

static SemaphoreHandle_t wifi_address_hint_mutex = nullptr;
static WifiOperationalAddressHint wifi_address_hint = {};

static void ensure_wifi_address_hint_mutex() {
    if (!wifi_address_hint_mutex) {
        wifi_address_hint_mutex = xSemaphoreCreateMutex();
    }
}

static bool copy_wifi_address_hint(WifiOperationalAddressHint *out) {
    if (!out) return false;
    ensure_wifi_address_hint_mutex();
    if (!wifi_address_hint_mutex) return false;
    if (xSemaphoreTake(wifi_address_hint_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    *out = wifi_address_hint;
    xSemaphoreGive(wifi_address_hint_mutex);
    return out->active &&
           (out->ip[0] != '\0' || out->scan_thread_address_cache) &&
           esp_timer_get_time() < out->deadline_us;
}

static bool find_thread_cache_operational_address(char *ip, size_t ip_len) {
    if (!ip || ip_len == 0) return false;
    ip[0] = '\0';

    if (!esp_openthread_lock_acquire(pdMS_TO_TICKS(20))) {
        return false;
    }

    bool found = false;
    otInstance *instance = esp_openthread_get_instance();
    if (instance) {
        const uint16_t own_rloc16 = otThreadGetRloc16(instance);
        otCacheEntryIterator iterator = {};
        otCacheEntryInfo entry = {};
        otCacheEntryInfo best_entry = {};
        uint32_t best_score = 0;
        while (otThreadGetNextCacheEntry(instance, &entry, &iterator) == OT_ERROR_NONE) {
            if (entry.mRloc16 == own_rloc16 || entry.mRloc16 == 0xfffe) {
                continue;
            }
            if (entry.mState != OT_CACHE_ENTRY_STATE_CACHED &&
                entry.mState != OT_CACHE_ENTRY_STATE_SNOOPED) {
                continue;
            }
            if (entry.mTarget.mFields.m8[0] != 0xfd) {
                continue;
            }

            const uint32_t score = entry.mState == OT_CACHE_ENTRY_STATE_CACHED
                                       ? (100000U + entry.mLastTransTime)
                                       : entry.mTimeout;
            if (!found || score >= best_score) {
                best_entry = entry;
                best_score = score;
                found = true;
            }
        }

        if (found) {
            otIp6AddressToString(&best_entry.mTarget, ip, static_cast<uint16_t>(ip_len));
            found = ip[0] != '\0';
        }
    }

    esp_openthread_lock_release();
    return found;
}

static void update_wifi_address_hint_ip(const char *ip) {
    if (!ip || !ip[0]) return;
    ensure_wifi_address_hint_mutex();
    if (!wifi_address_hint_mutex) return;
    if (xSemaphoreTake(wifi_address_hint_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    if (wifi_address_hint.active && strcmp(wifi_address_hint.ip, ip) != 0) {
        snprintf(wifi_address_hint.ip, sizeof(wifi_address_hint.ip), "%s", ip);
    }
    xSemaphoreGive(wifi_address_hint_mutex);
}

static void inject_wifi_operational_address_hint(intptr_t) {
    WifiOperationalAddressHint hint = {};
    if (!copy_wifi_address_hint(&hint)) return;

    char cache_ip[sizeof(hint.ip)] = {};
    if (hint.scan_thread_address_cache && find_thread_cache_operational_address(cache_ip, sizeof(cache_ip))) {
        if (strcmp(hint.ip, cache_ip) != 0) {
            ESP_LOGI(TAG, "Using Thread address-cache operational hint for node 0x%" PRIX64 ": %s",
                     hint.node_id, cache_ip);
        }
        snprintf(hint.ip, sizeof(hint.ip), "%s", cache_ip);
        update_wifi_address_hint_ip(cache_ip);
    }

    if (hint.ip[0] == '\0') {
        return;
    }

    auto *commissioner = esp_matter::controller::matter_controller_client::get_instance().get_commissioner();
    if (!commissioner || !commissioner->GetFabricTable()) {
        ESP_LOGW(TAG, "Cannot inject operational address hint without commissioner fabric table");
        return;
    }

    const chip::FabricIndex fabric_index = commissioner->GetFabricIndex();
    const chip::FabricInfo *fabric = commissioner->GetFabricTable()->FindFabricWithIndex(fabric_index);
    if (!fabric) {
        ESP_LOGW(TAG, "Cannot inject operational address hint without fabric index %u",
                 static_cast<unsigned>(fabric_index));
        return;
    }

    chip::Inet::IPAddress ip_address;
    if (!chip::Inet::IPAddress::FromString(hint.ip, ip_address)) {
        ESP_LOGW(TAG, "Ignoring invalid operational address hint: %s", hint.ip);
        return;
    }

    chip::Dnssd::ResolvedNodeData node_data;
    node_data.operationalData.peerId = chip::PeerId()
                                           .SetCompressedFabricId(fabric->GetCompressedFabricId())
                                           .SetNodeId(hint.node_id);
    node_data.resolutionData.port = hint.port ? hint.port : DEFAULT_MATTER_OPERATIONAL_PORT;
    node_data.resolutionData.numIPs = 1;
    node_data.resolutionData.ipAddress[0] = ip_address;
    chip::Platform::CopyString(node_data.resolutionData.hostName, "old-macdonald-ap-hint");

    ESP_LOGI(TAG, "Injecting Matter operational address hint for node 0x%" PRIX64 " at %s:%u",
             hint.node_id, hint.ip, node_data.resolutionData.port);
    auto &resolver = static_cast<chip::AddressResolve::Impl::Resolver &>(
        chip::AddressResolve::Resolver::Instance());
    resolver.OnOperationalNodeResolved(node_data);
}

static void wifi_address_hint_task(void *) {
    while (true) {
        WifiOperationalAddressHint hint = {};
        if (!copy_wifi_address_hint(&hint)) {
            break;
        }

        chip::DeviceLayer::PlatformMgr().ScheduleWork(inject_wifi_operational_address_hint, 0);
        vTaskDelay(OPERATIONAL_ADDRESS_HINT_INTERVAL);
    }

    ensure_wifi_address_hint_mutex();
    if (wifi_address_hint_mutex && xSemaphoreTake(wifi_address_hint_mutex, portMAX_DELAY) == pdTRUE) {
        wifi_address_hint.active = false;
        wifi_address_hint.task_running = false;
        xSemaphoreGive(wifi_address_hint_mutex);
    }
    vTaskDelete(nullptr);
}

static void start_wifi_address_hint_task_locked() {
    if (wifi_address_hint.task_running) return;
    wifi_address_hint.task_running = true;
    if (xTaskCreate(wifi_address_hint_task, "matter_ip_hint", OPERATIONAL_ADDRESS_HINT_TASK_STACK, nullptr,
                    tskIDLE_PRIORITY + 2, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start Matter operational address hint task");
        wifi_address_hint.task_running = false;
    }
}

esp_err_t matter_controller_init(const uint64_t node_id, const uint64_t fabric_id, const uint16_t listen_port,
                                 void (*read_attribute_data_callback)(
                                     uint64_t,
                                     const chip::app::ConcreteDataAttributePath &,
                                     chip::TLV::TLVReader *),
                                void (*subscribe_done_callback)(uint64_t remote_node_id, uint32_t subscription_id)
                                ) {
    if (!read_attribute_data_callback || !subscribe_done_callback) {
        ESP_LOGE(TAG, "Invalid read attribute callback");
        return ESP_ERR_INVALID_ARG;
    }

    attribute_report_cb = read_attribute_data_callback;
    subscribe_done_cb = subscribe_done_callback;

    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "Initializing Matter controller client");
    esp_matter::lock::status_t lock_status = esp_matter::lock::chip_stack_lock(MATTER_CONTROLLER_LOCK_TIMEOUT);
    if (lock_status != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Timed out waiting for CHIP stack lock during controller init");
        return ESP_ERR_TIMEOUT;
    }

    err = esp_matter::controller::matter_controller_client::get_instance().init(node_id, fabric_id, listen_port);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Controller client initialization failed: 0x%x", err);
        goto exit;
    }

#ifdef CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
    ESP_LOGI(TAG, "Setting up commissioner");
    err = esp_matter::controller::matter_controller_client::get_instance().setup_commissioner();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Commissioner setup failed: 0x%x", err);
        goto exit;
    }
#endif

    exit:
        esp_matter::lock::chip_stack_unlock();
    return err;
}

esp_err_t pairing_ble_thread(const uint64_t node_id, const uint32_t pin, const uint16_t discriminator,
                             uint8_t *dataset_tlvs,
                             const size_t dataset_len) {
    // Validate dataset parameters
    if (dataset_len > 0 && !dataset_tlvs) {
        ESP_LOGE(TAG, "Invalid dataset parameters");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting BLE Thread pairing with node 0x%" PRIX64, node_id);
    esp_matter::lock::status_t lock_status = esp_matter::lock::chip_stack_lock(MATTER_CONTROLLER_LOCK_TIMEOUT);
    if (lock_status != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Timed out waiting for CHIP stack lock during BLE Thread pairing");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = esp_matter::controller::pairing_ble_thread(node_id,
                                                               pin,
                                                               discriminator,
                                                               dataset_tlvs,
                                                               dataset_len);
    esp_matter::lock::chip_stack_unlock();
    return err;
}

esp_err_t pairing_ble_wifi(const uint64_t node_id,
                           const uint32_t pin,
                           const uint16_t discriminator,
                           const char *ssid,
                           const char *password) {
    if (!ssid || !ssid[0] || !password) {
        ESP_LOGE(TAG, "Invalid Wi-Fi commissioning credentials");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting BLE Wi-Fi pairing with node 0x%" PRIX64, node_id);
    esp_matter::lock::status_t lock_status = esp_matter::lock::chip_stack_lock(MATTER_CONTROLLER_LOCK_TIMEOUT);
    if (lock_status != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Timed out waiting for CHIP stack lock during BLE Wi-Fi pairing");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = esp_matter::controller::pairing_ble_wifi(node_id, pin, discriminator, ssid, password);
    esp_matter::lock::chip_stack_unlock();
    return err;
}

void matter_controller_prepare_wifi_operational_address_hint(const uint64_t node_id, const uint16_t port) {
    ensure_wifi_address_hint_mutex();
    if (!wifi_address_hint_mutex) return;
    if (xSemaphoreTake(wifi_address_hint_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    wifi_address_hint.active = true;
    wifi_address_hint.node_id = node_id;
    wifi_address_hint.port = port ? port : DEFAULT_MATTER_OPERATIONAL_PORT;
    wifi_address_hint.ip[0] = '\0';
    wifi_address_hint.deadline_us = esp_timer_get_time() + OPERATIONAL_ADDRESS_HINT_WINDOW_US;
    wifi_address_hint.scan_thread_address_cache = false;
    xSemaphoreGive(wifi_address_hint_mutex);

    ESP_LOGI(TAG, "Prepared Matter operational address hint window for node 0x%" PRIX64, node_id);
}

void matter_controller_prepare_thread_operational_address_hint(const uint64_t node_id, const uint16_t port) {
    ensure_wifi_address_hint_mutex();
    if (!wifi_address_hint_mutex) return;
    if (xSemaphoreTake(wifi_address_hint_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    wifi_address_hint.active = true;
    wifi_address_hint.node_id = node_id;
    wifi_address_hint.port = port ? port : DEFAULT_MATTER_OPERATIONAL_PORT;
    wifi_address_hint.ip[0] = '\0';
    wifi_address_hint.deadline_us = esp_timer_get_time() + OPERATIONAL_ADDRESS_HINT_WINDOW_US;
    wifi_address_hint.scan_thread_address_cache = true;
    start_wifi_address_hint_task_locked();
    xSemaphoreGive(wifi_address_hint_mutex);

    ESP_LOGI(TAG, "Prepared Thread address-cache Matter operational hint window for node 0x%" PRIX64,
             node_id);
}

void matter_controller_prepare_operational_address_hint(const uint64_t node_id, const char *ip, const uint16_t port) {
    if (!ip || !ip[0]) return;

    chip::Inet::IPAddress ip_address;
    if (!chip::Inet::IPAddress::FromString(ip, ip_address)) {
        ESP_LOGW(TAG, "Ignoring invalid Matter operational address hint: %s", ip);
        return;
    }

    ensure_wifi_address_hint_mutex();
    if (!wifi_address_hint_mutex) return;
    if (xSemaphoreTake(wifi_address_hint_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    wifi_address_hint.active = true;
    wifi_address_hint.node_id = node_id;
    wifi_address_hint.port = port ? port : DEFAULT_MATTER_OPERATIONAL_PORT;
    snprintf(wifi_address_hint.ip, sizeof(wifi_address_hint.ip), "%s", ip);
    wifi_address_hint.deadline_us = esp_timer_get_time() + OPERATIONAL_ADDRESS_HINT_WINDOW_US;
    wifi_address_hint.scan_thread_address_cache = (ip_address.Type() == chip::Inet::IPAddressType::kIPv6);
    start_wifi_address_hint_task_locked();
    xSemaphoreGive(wifi_address_hint_mutex);

    ESP_LOGI(TAG, "Prepared explicit Matter operational address hint for node 0x%" PRIX64 " at %s:%u",
             node_id, ip, port ? port : DEFAULT_MATTER_OPERATIONAL_PORT);
}

void matter_controller_note_ap_sta_ip(const esp_ip4_addr_t *ip) {
    if (!ip) return;

    char ip_text[16] = {};
    esp_ip4addr_ntoa(ip, ip_text, sizeof(ip_text));
    if (strcmp(ip_text, "192.168.4.1") == 0 || strcmp(ip_text, "0.0.0.0") == 0) {
        return;
    }

    ensure_wifi_address_hint_mutex();
    if (!wifi_address_hint_mutex) return;
    if (xSemaphoreTake(wifi_address_hint_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    if (wifi_address_hint.active && esp_timer_get_time() < wifi_address_hint.deadline_us) {
        snprintf(wifi_address_hint.ip, sizeof(wifi_address_hint.ip), "%s", ip_text);
        ESP_LOGI(TAG, "Captured AP station IP %s as Matter operational address hint for node 0x%" PRIX64,
                 wifi_address_hint.ip, wifi_address_hint.node_id);
        start_wifi_address_hint_task_locked();
    }
    xSemaphoreGive(wifi_address_hint_mutex);
}

void matter_controller_clear_wifi_operational_address_hint(void) {
    ensure_wifi_address_hint_mutex();
    if (!wifi_address_hint_mutex) return;
    if (xSemaphoreTake(wifi_address_hint_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    wifi_address_hint.active = false;
    wifi_address_hint.ip[0] = '\0';
    wifi_address_hint.deadline_us = 0;
    xSemaphoreGive(wifi_address_hint_mutex);
    ESP_LOGI(TAG, "Cleared Matter operational address hint");
}

esp_err_t invoke_cluster_command(const uint64_t destination_id, const uint16_t endpoint_id, const uint32_t cluster_id,
                                 const uint32_t command_id, const char *command_data_field) {
    if (!command_data_field) {
        ESP_LOGE(TAG, "Invalid command data field");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending cluster invoke command");

    // Lock the CHIP stack for thread-safe access
    esp_matter::lock::status_t lock_status = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    if (lock_status != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Failed to lock Chip stack");
        return ESP_ERR_INVALID_STATE;
    }

    // Send the command
    esp_err_t err = esp_matter::controller::send_invoke_cluster_command(destination_id, endpoint_id, cluster_id,
                                                                        command_id, command_data_field);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send invoke command: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Cluster invoke command sent successfully");
    }

    // Unlock the CHIP stack
    esp_matter::lock::chip_stack_unlock();

    return err;
}

esp_err_t send_subscribe_attr_command(uint64_t node_id, const uint16_t endpoint_id, const uint32_t cluster_id,
                                      const uint32_t attribute_id, uint16_t min_interval, uint16_t max_interval,
                                      bool auto_resubscribe) {

    // Allocate memory for attribute path
    ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
    attr_paths.Alloc(1);
    if (!attr_paths.Get()) {
        ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
        return ESP_ERR_NO_MEM;
    }
    attr_paths[0] = AttributePathParams(endpoint_id, cluster_id, attribute_id);

    // Empty event path array (not subscribing to events)
    ScopedMemoryBufferWithSize<EventPathParams> event_paths;
    event_paths.Alloc(0);

    // Lock CHIP stack before creating command
    esp_matter::lock::status_t lock_status = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    if (lock_status != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Failed to lock Chip stack");
        return ESP_ERR_INVALID_STATE;
    }

    // Create and initialize the subscription command
    auto *cmd = chip::Platform::New<esp_matter::controller::subscribe_command>(
        node_id,
        std::move(attr_paths),
        std::move(event_paths),
        min_interval,
        max_interval,
        auto_resubscribe,
        attribute_report_cb,
        nullptr,
        subscribe_done_cb,
        nullptr
    );
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to alloc memory for subscribe_command");
        esp_matter::lock::chip_stack_unlock();
        return ESP_ERR_NO_MEM;
    }

    // Send the subscription command
    esp_err_t err = cmd->send_command();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send subscribe attr command: %s", esp_err_to_name(err));
        chip::Platform::Delete(cmd);
    } else {
        ESP_LOGI(TAG, "Subscribe attr command sent successfully");
    }

    // Unlock the CHIP stack
    esp_matter::lock::chip_stack_unlock();

    return err;
}

esp_err_t send_read_attr_command(uint64_t node_id, const uint16_t endpoint_id, const uint32_t cluster_id,
                                 const uint32_t attribute_id) {

    // Allocate memory for an attribute path
    ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
    attr_paths.Alloc(1);
    if (!attr_paths.Get()) {
        ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
        return ESP_ERR_NO_MEM;
    }
    attr_paths[0] = AttributePathParams(endpoint_id, cluster_id, attribute_id);

    // Empty event path array (not reading events)
    ScopedMemoryBufferWithSize<EventPathParams> event_paths;
    event_paths.Alloc(0);

    // Lock CHIP stack
    if (esp_matter::lock::chip_stack_lock(portMAX_DELAY) != esp_matter::lock::SUCCESS) {
        ESP_LOGE(TAG, "Failed to lock CHIP stack");
        return ESP_ERR_INVALID_STATE;
    }

    // Create and initialize the read command
    esp_err_t err = ESP_OK;
    auto *cmd = chip::Platform::New<esp_matter::controller::read_command>(
        node_id, std::move(attr_paths), std::move(event_paths),
        attribute_report_cb, nullptr, nullptr);
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to alloc memory for read_command");
        err = ESP_ERR_NO_MEM;
    } else {
        err = cmd->send_command();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send read command: %s", esp_err_to_name(err));
            chip::Platform::Delete(cmd);
        }
    }

    // Unlock the CHIP stack
    esp_matter::lock::chip_stack_unlock();

    return err;
}
