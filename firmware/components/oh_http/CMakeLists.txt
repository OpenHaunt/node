idf_component_register(
    SRCS "oh_http.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES oh_app oh_audio oh_ota oh_sacn oh_status oh_node app_update oh_core oh_config oh_net oh_board oh_clock esp_http_server esp_timer json espcoredump spi_flash
    EMBED_TXTFILES "www/index.html")
