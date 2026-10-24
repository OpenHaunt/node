idf_component_register(
    SRCS "oh_config.c"
    INCLUDE_DIRS "include"
    REQUIRES oh_core json
    PRIV_REQUIRES oh_node nvs_flash esp_netif)
