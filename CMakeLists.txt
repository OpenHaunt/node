cmake_minimum_required(VERSION 3.16)

# One source tree, several boards. The board is a cache variable so that
#   idf.py -B build/<board> -DBOARD=<board> build
# keeps one build directory and one sdkconfig per board, side by side.
set(BOARD "esp32c6-devkitc-1" CACHE STRING "Board directory under boards/")
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}/sdkconfig.board")
    message(FATAL_ERROR "Unknown BOARD '${BOARD}'. Expected a directory under boards/ with an sdkconfig.board.")
endif()

set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;boards/${BOARD}/sdkconfig.board")
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(openhaunt-node)
