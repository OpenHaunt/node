idf_component_register(
    SRCS "oh_console.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES oh_app oh_node oh_core oh_config oh_net oh_board oh_clock oh_display oh_module_eeprom oh_ota oh_sacn console json)
