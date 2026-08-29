idf_component_register(
    SRCS "oh_drivers.c" "gpio_in.c" "gpio_out.c"
    INCLUDE_DIRS "include"
    REQUIRES oh_node oh_core
    PRIV_REQUIRES esp_driver_gpio esp_timer json)
