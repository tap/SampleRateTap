# Standalone Raspberry Pi Pico 2 (RP2350) firmware: dual-core deployment of
# the ASRC — producer on core0, consumer on core1, one core per clock domain
# (docs/HARDWARE_TESTING.md, Setup 2, "Dual-core deployment"). Deliberately
# NOT wired into the root build — configure this directory on its own:
#
#   cmake -B build -DPICO_BOARD=pico2
#   cmake --build build -j
#
# The Pico SDK is fetched by git tag rather than release-tarball URL+SHA256:
# GitHub source tarballs exclude submodules, and USB CDC stdio needs
# lib/tinyusb. GIT_SUBMODULES limits the clone to that one submodule
# (lwip/btstack/mbedtls/cyw43 are not used here).
cmake_minimum_required(VERSION 3.24)

set(PICO_BOARD pico2 CACHE STRING "Pico SDK board")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Pico SDK platform (secure Arm)")

# The library's constructors throw (allocation, filter design); the
# 12-channel phase relies on catching bad_alloc instead of crashing. The SDK
# default is -fno-exceptions.
set(PICO_CXX_ENABLE_EXCEPTIONS 1 CACHE BOOL "")

include(FetchContent)
FetchContent_Declare(
    pico_sdk
    GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk.git
    GIT_TAG 2.1.1 # first-class RP2350 support
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "lib/tinyusb")
FetchContent_GetProperties(pico_sdk)
if(NOT pico_sdk_POPULATED)
    FetchContent_Populate(pico_sdk)
endif()
# Must be included before project() so the SDK can inject its toolchain file.
include(${pico_sdk_SOURCE_DIR}/pico_sdk_init.cmake)

project(pico2_dualcore C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

add_executable(pico2_dualcore main.cpp)
# Self-contained except for the header-only library itself.
target_include_directories(pico2_dualcore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_link_libraries(pico2_dualcore PRIVATE pico_stdlib pico_multicore cmsis_core)

# Telemetry prints from the producer core; when the USB host stops draining
# the CDC buffer, stdio_usb blocks the writer for up to this long before
# dropping output (SDK default: 500 ms — far more than the FIFO can absorb).
# Capped at 2 ms so the worst push stall stays inside the FIFO setpoint
# budget; see the kTargetLatencyFrames comment in main.cpp.
target_compile_definitions(pico2_dualcore PRIVATE PICO_STDIO_USB_STDOUT_TIMEOUT_US=2000)

pico_enable_stdio_usb(pico2_dualcore 1)
pico_enable_stdio_uart(pico2_dualcore 0)
pico_add_extra_outputs(pico2_dualcore) # .uf2 etc.
