idf_component_register(
    SRCS "oh_audio.c" "oh_clips.c"
    INCLUDE_DIRS "include"
    REQUIRES oh_core json
    PRIV_REQUIRES oh_board oh_clock oh_node esp_driver_i2s esp_driver_gpio esp_timer fatfs wear_levelling
                  mbedtls lwip esp_psram)
