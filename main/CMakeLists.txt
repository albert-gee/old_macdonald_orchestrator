idf_component_register(
        SRC_DIRS "src" "src/event_handlers" "src/commands" "src/json"
        INCLUDE_DIRS "include"
        LDFRAGMENTS "linker.lf" # https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/linker-script-generation.html
)

if (CONFIG_SPIFFS_ATTESTATION_TRUST_STORE)
    spiffs_create_partition_image(paa_cert ${CMAKE_SOURCE_DIR}/paa_cert FLASH_IN_PROJECT)
endif ()

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
