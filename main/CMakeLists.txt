idf_component_register(
        SRC_DIRS "src" "src/event_handlers" "src/commands" "src/messages" "src/state" "src/registry" "src/control" "src/matter" "src/storage"
        INCLUDE_DIRS "include"
        LDFRAGMENTS "linker.lf" # https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/linker-script-generation.html
)

execute_process(
        COMMAND git rev-parse --short=12 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ORCHESTRATOR_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
)
execute_process(
        COMMAND git status --short
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ORCHESTRATOR_GIT_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
)
if (NOT ORCHESTRATOR_GIT_COMMIT)
    set(ORCHESTRATOR_GIT_COMMIT "unknown")
endif ()
if (ORCHESTRATOR_GIT_STATUS)
    set(ORCHESTRATOR_GIT_COMMIT "${ORCHESTRATOR_GIT_COMMIT}-dirty")
endif ()
target_compile_definitions(${COMPONENT_LIB} PRIVATE ORCHESTRATOR_GIT_COMMIT="${ORCHESTRATOR_GIT_COMMIT}")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=nvs_open")

if (CONFIG_SPIFFS_ATTESTATION_TRUST_STORE)
    spiffs_create_partition_image(paa_cert ${CMAKE_SOURCE_DIR}/paa_cert FLASH_IN_PROJECT)
endif ()

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
