set(srcs "oh_net.c" "oh_captive_dns.c" "oh_mdns.c")
if(CONFIG_OH_TRANSPORT_ETH)
    list(APPEND srcs "oh_eth.c")
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES oh_core json esp_netif esp_event
    PRIV_REQUIRES oh_node oh_config esp_wifi esp_timer lwip esp_eth esp_driver_gpio esp_driver_spi esp_hw_support)
