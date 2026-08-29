idf_component_register(
    SRCS "oh_node.c" "oh_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES oh_core esp_event json
    PRIV_REQUIRES oh_clock esp_timer)
