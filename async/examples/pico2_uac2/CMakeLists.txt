# Standalone Raspberry Pi Pico 2 (RP2350) firmware: a USB Audio Class 2.0
# asynchronous-mode device on the Pico's own crystal, the hardware bench's
# independent clock (docs/HARDWARE_TESTING.md; the family roadmap's A11).
# Deliberately NOT wired into the root build — configure this directory on
# its own:
#
#   cmake -B build -DPICO_BOARD=pico2
#   cmake --build build -j
#
# The Pico SDK is fetched as pico2_cyccnt fetches it (its CMakeLists.txt says
# why by tag): its lib/tinyusb is the USB stack here.
cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(PICO_BOARD pico2 CACHE STRING "Pico SDK board")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Pico SDK platform (secure Arm)")

include(FetchContent)
FetchContent_Declare(
    pico_sdk
    GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk.git
    GIT_TAG 2.1.1
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "lib/tinyusb")
FetchContent_GetProperties(pico_sdk)
if(NOT pico_sdk_POPULATED)
    FetchContent_Populate(pico_sdk)
endif()
include(${pico_sdk_SOURCE_DIR}/pico_sdk_init.cmake)

project(pico2_uac2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# A known offset on the device's sample clock, on top of the crystal's own
# error: two runs at different settings differ by exactly this, which checks
# the bench's fit (README). async's charter bounds it at ±1000 ppm.
set(PICO2_UAC2_PPM 0 CACHE STRING "Offset injected on the device's sample clock, in ppm (-1000..1000)")
if(NOT PICO2_UAC2_PPM MATCHES "^-?[0-9]+$" OR PICO2_UAC2_PPM LESS -1000 OR PICO2_UAC2_PPM GREATER 1000)
    message(FATAL_ERROR "PICO2_UAC2_PPM must be an integer in -1000..1000, not '${PICO2_UAC2_PPM}'")
endif()

add_executable(pico2_uac2 main.cpp usb_descriptors.c)
# tusb_config.h and usb_descriptors.h live here.
target_include_directories(pico2_uac2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(pico2_uac2 PRIVATE PICO2_UAC2_PPM=${PICO2_UAC2_PPM})
target_link_libraries(pico2_uac2 PRIVATE pico_stdlib pico_multicore pico_unique_id tinyusb_device)

# The USB device is the audio function, so stdio goes to UART0 (GP0/GP1).
pico_enable_stdio_usb(pico2_uac2 0)
pico_enable_stdio_uart(pico2_uac2 1)
pico_add_extra_outputs(pico2_uac2) # .uf2 etc.
