idf_component_register(
    SRCS "oh_http.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES oh_app oh_node oh_core oh_config oh_net oh_board oh_clock esp_http_server json
    EMBED_TXTFILES "www/index.html")
