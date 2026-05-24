if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/certs/servercert.pem")
    message(FATAL_ERROR "Missing development TLS certificate. Generate it using components/websocket_server/certs/cert_generate.sh. Do not commit generated certs.")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/certs/prvtkey.pem")
    message(FATAL_ERROR "Missing development TLS private key. Generate it using components/websocket_server/certs/cert_generate.sh. Do not commit generated keys.")
endif()

idf_component_register(
        SRC_DIRS "src"
        INCLUDE_DIRS "include"
        REQUIRES esp_https_server esp_timer esp_event esp_wifi
        EMBED_TXTFILES "certs/servercert.pem" "certs/prvtkey.pem"
)
