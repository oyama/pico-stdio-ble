cmake_minimum_required(VERSION 3.13...3.27)
include(vendor/pico_sdk_import.cmake)
include(pico_stdio_ble.cmake)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
project(pico_stdio_ble C CXX ASM)
pico_sdk_init()


add_library(pico_stdio_ble INTERFACE)
target_sources(pico_stdio_ble INTERFACE stdio_ble.c)
pico_btstack_make_gatt_header(pico_stdio_ble INTERFACE "${CMAKE_CURRENT_LIST_DIR}/pico_stdio_ble.gatt")
target_include_directories(pico_stdio_ble INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(pico_stdio_ble INTERFACE
    pico_stdio
    pico_btstack_ble
    pico_btstack_cyw43
    pico_cyw43_arch_none
    )

add_executable(pico_stdio_ble_demo EXCLUDE_FROM_ALL demo.c)
target_link_libraries(pico_stdio_ble_demo PRIVATE pico_stdlib)

pico_enable_stdio_usb(pico_stdio_ble_demo 1)
pico_enable_stdio_ble(pico_stdio_ble_demo 1)
pico_add_extra_outputs(pico_stdio_ble_demo)
