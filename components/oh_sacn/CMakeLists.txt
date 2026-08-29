idf_component_register(
    SRCS "oh_sacn.c"
    INCLUDE_DIRS "include"
    REQUIRES oh_core
    PRIV_REQUIRES oh_node oh_config oh_net lwip esp_event)
