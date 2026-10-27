idf_component_register(
    SRCS "oh_status.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES oh_node oh_board oh_config json esp_timer esp_driver_gpio esp_event)
