if(CONFIG_OH_DISPLAY)
    set(srcs "oh_display.c" "oh_fb.c")
else()
    set(srcs "oh_fb.c")
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include" "."
    PRIV_REQUIRES oh_font oh_app oh_node oh_core oh_config oh_net oh_clock oh_mqtt esp_lcd esp_driver_spi esp_driver_gpio esp_timer nvs_flash esp_event)
