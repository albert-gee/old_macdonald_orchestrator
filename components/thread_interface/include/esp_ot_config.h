#pragma once

#include "sdkconfig.h"
#include "esp_openthread_types.h"

// Default configuration for OpenThread Radio Co-Processor (RCP) over UART.
// Assumes CONFIG_OPENTHREAD_RADIO_SPINEL_UART is enabled.
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                      \
{                                                                   \
    .radio_mode = RADIO_MODE_UART_RCP,                              \
    .radio_uart_config = {                                          \
        .port = UART_NUM_1,                                         \
        .uart_config = {                                             \
            .baud_rate = 460800,                                    \
            .data_bits = UART_DATA_8_BITS,                          \
            .parity = UART_PARITY_DISABLE,                          \
            .stop_bits = UART_STOP_BITS_1,                          \
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,                  \
            .rx_flow_ctrl_thresh = 0,                               \
            .source_clk = UART_SCLK_DEFAULT,                        \
        },                                                          \
        .rx_pin = GPIO_NUM_17,                                      \
        .tx_pin = GPIO_NUM_18,                                      \
    },                                                              \
}

// Default configuration for OpenThread Host connection (no host connection by default).
#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                        \
{                                                                   \
    .host_connection_mode = HOST_CONNECTION_MODE_NONE,             \
}

// Default port configuration for OpenThread (NVS partition, netif and task queues).
#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                        \
{                                                                   \
    .storage_partition_name = "nvs",                                \
    .netif_queue_size = 10,                                         \
    .task_queue_size = 10,                                          \
}

// Default OpenThread configuration combining radio, host, and port settings.
#define ESP_OPENTHREAD_DEFAULT_CONFIG()                              \
{                                                                   \
    .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),          \
    .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),            \
    .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()             \
}

// Configuration for RCP firmware update on ESP32H2 UART.
#define ESP_OPENTHREAD_RCP_UPDATE_CONFIG()                           \
{                                                                   \
    .rcp_type = RCP_TYPE_ESP32H2_UART,                              \
    .uart_rx_pin = 17,                                              \
    .uart_tx_pin = 18,                                              \
    .uart_port = 1,                                                 \
    .uart_baudrate = 115200,                                        \
    .reset_pin = 7,                                                 \
    .boot_pin = 8,                                                  \
    .update_baudrate = 460800,                                      \
    .firmware_dir = "/rcp_fw/ot_rcp",                               \
    .target_chip = ESP32H2_CHIP,                                    \
}

// Configuration structure for registering SPIFFS filesystem for RCP firmware.
#define ESP_VFS_SPIFFS_REGISTER_CONFIG()                             \
{                                                                   \
    .base_path = "/rcp_fw",                                         \
    .partition_label = "rcp_fw",                                    \
    .max_files = 10,                                                \
    .format_if_mount_failed = false,                                \
}
