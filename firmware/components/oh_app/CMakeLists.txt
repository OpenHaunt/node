idf_component_register(
    SRCS "oh_app.c"
    INCLUDE_DIRS "include"
    REQUIRES oh_core oh_node
    PRIV_REQUIRES oh_board oh_config oh_drivers oh_net oh_module_eeprom oh_ota oh_sacn esp_app_format esp_timer esp_event)
