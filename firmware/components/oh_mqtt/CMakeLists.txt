idf_component_register(
    SRCS "oh_mqtt.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES oh_app oh_audio oh_node oh_core oh_config oh_net oh_clock mqtt esp_timer esp_event json esp_driver_tsens lwip)
