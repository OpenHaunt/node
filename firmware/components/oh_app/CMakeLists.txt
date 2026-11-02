idf_component_register(
    SRCS "oh_app.c"
    INCLUDE_DIRS "include"
    REQUIRES oh_core oh_node
    PRIV_REQUIRES oh_board oh_config oh_drivers oh_net oh_module_eeprom oh_ota oh_sacn esp_app_format esp_timer esp_event)

# The build ID and time (oh_app_build_id/oh_app_build_time). The file is written
# once here so the target has a source at configure time, then again on every build.
set(build_info "${CMAKE_CURRENT_BINARY_DIR}/oh_build_info.c")
set(build_info_args -DSRC=${CMAKE_CURRENT_LIST_DIR} -DOUT=${build_info} -P ${CMAKE_CURRENT_LIST_DIR}/build_info.cmake)
execute_process(COMMAND ${CMAKE_COMMAND} ${build_info_args})
add_custom_target(oh_build_info COMMAND ${CMAKE_COMMAND} ${build_info_args} BYPRODUCTS ${build_info} VERBATIM)
target_sources(${COMPONENT_LIB} PRIVATE ${build_info})
add_dependencies(${COMPONENT_LIB} oh_build_info)
