idf_component_register(
        SRC_DIRS "src"
        INCLUDE_DIRS "include"
        REQUIRES esp_https_server esp_timer esp_event esp_wifi
        EMBED_TXTFILES "certs/servercert.pem" "certs/prvtkey.pem"
)
