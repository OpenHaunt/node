file(READ ${CMAKE_CURRENT_LIST_DIR}/../version.txt OH_VERSION)
string(STRIP "${OH_VERSION}" OH_VERSION)
idf_component_register(SRCS "app_main.c" INCLUDE_DIRS "" REQUIRES oh_board oh_core nvs_flash esp_app_format)
target_compile_definitions(${COMPONENT_LIB} PRIVATE OH_FW_VERSION="${OH_VERSION}")
