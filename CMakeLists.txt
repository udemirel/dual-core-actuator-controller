cmake_minimum_required(VERSION 3.13)

# Target hardware and architecture configuration (RP2350 Dual ARM Cortex-M33)
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)

# Point to the local submodule SDK
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/external/pico-sdk)
include(pico_sdk_import.cmake)

project(rp2350_safety_multicore C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(${PROJECT_NAME}
    src/main.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    pico_multicore
    hardware_sync
)

# Enable USB CDC for deterministic telemetry logging
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})