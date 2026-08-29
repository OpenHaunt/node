idf_component_register(
    SRCS "oh_module_eeprom.c"
    INCLUDE_DIRS "include"
    REQUIRES oh_core
    PRIV_REQUIRES oh_i2c oh_board oh_config esp_driver_gpio)
