idf_component_register(
    SRCS "app_main.c"
    PRIV_REQUIRES oh_app oh_audio oh_node oh_http oh_mqtt oh_status oh_display oh_console nvs_flash esp_event)
