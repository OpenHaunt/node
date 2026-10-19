idf_component_register(
    SRCS "oh_drivers.c" "gpio_in.c" "gpio_out.c" "ws2812.c" "mcp23017.c" "sht4x.c" "ssd1306.c" "dmx_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES oh_node oh_core
    PRIV_REQUIRES oh_i2c oh_font oh_sacn oh_clock esp_driver_gpio esp_driver_uart esp_timer esp_lcd json)
