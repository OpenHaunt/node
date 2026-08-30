# Host-side tests for the pure-C core. No ESP-IDF: only its vendored cJSON and
# Unity sources are borrowed, via $IDF_PATH (or the default install path).
#
#   cmake -S test/host -B build/host && cmake --build build/host && ctest --test-dir build/host
cmake_minimum_required(VERSION 3.16)
project(oh_core_host_tests C)

if(NOT DEFINED ENV{IDF_PATH})
    set(IDF_PATH "$ENV{HOME}/.espressif/v5.5.3/esp-idf")
else()
    set(IDF_PATH "$ENV{IDF_PATH}")
endif()

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/oh_core)

add_library(cjson STATIC ${IDF_PATH}/components/json/cJSON/cJSON.c)
target_include_directories(cjson PUBLIC ${IDF_PATH}/components/json/cJSON)

add_library(unity STATIC ${IDF_PATH}/components/unity/unity/src/unity.c)
target_include_directories(unity PUBLIC ${IDF_PATH}/components/unity/unity/src)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)

file(GLOB CORE_SRCS ${CORE}/src/*.c)
add_library(oh_core STATIC ${CORE_SRCS})
target_include_directories(oh_core PUBLIC ${CORE}/include)
target_link_libraries(oh_core PUBLIC cjson m)
target_compile_options(oh_core PRIVATE -std=gnu11 -Wall -Wextra -Werror)

enable_testing()
foreach(t descriptor json vmod debounce e131 curve effect transition clock_sync)
    add_executable(test_${t} test_${t}.c)
    target_link_libraries(test_${t} oh_core unity)
    target_compile_options(test_${t} PRIVATE -std=gnu11 -Wall -Wextra)
    add_test(NAME ${t} COMMAND test_${t})
endforeach()
