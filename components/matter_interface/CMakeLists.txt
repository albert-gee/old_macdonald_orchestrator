idf_component_register(
        SRC_DIRS "src"
        INCLUDE_DIRS "include"
        REQUIRES "esp_matter_controller" "esp_matter_console" "esp_matter" "nvs_flash" "heap" "esp_system"
)
