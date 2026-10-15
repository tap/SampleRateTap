# Standalone Raspberry Pi Pico 2 (RP2350) firmware: DWT.CYCCNT cycle
# measurement of the ASRC hot path on real Cortex-M33 silicon
# (docs/HARDWARE_TESTING.md, Setup 2). Deliberately NOT wired into the root
# build — configure this directory on its own:
#
#   cmake -B build -DPICO_BOARD=pico2
#   cmake --build build -j
#
# The Pico SDK is fetched by git tag rather than release-tarball URL+SHA256:
# GitHub source tarballs exclude submodules, and USB CDC stdio needs
# lib/tinyusb. GIT_SUBMODULES limits the clone to that one submodule
# (lwip/btstack/mbedtls/cyw43 are not used here).
cmake_minimum_required(VERSION 3.24)

# Release unless asked otherwise: the M33 ratchet's workloads are Release
# (cmake/arm-cortex-m33-mps2.cmake), and the cycles here calibrate them.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(PICO_BOARD pico2 CACHE STRING "Pico SDK board")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Pico SDK platform (secure Arm)")

# Exceptions are ON by default here, against the SDK's -fno-exceptions
# default: the 12-channel case relies on catching bad_alloc so a tighter
# build prints SKIP instead of hard-faulting. The firmware also builds with
# them off (-DPICO_CXX_ENABLE_EXCEPTIONS=0, what CI's second build checks,
# the family's error model, A3): the configuration is then checked through
# tap::sr::async::config::validate() before constructing, and a refused
# allocation is a hard fault, as in any -fno-exceptions firmware.
set(PICO_CXX_ENABLE_EXCEPTIONS 1 CACHE BOOL "C++ exceptions in the firmware (0: validate() before constructing)")

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

project(pico2_cyccnt C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# Float datapath measurement: soft-double accumulation on the M33, expected
# ~3.8x the Q15 instruction count (1,856.7M vs 484.1M baselines) — slow
# but a real number is still valuable.
option(PICO2_MEASURE_FLOAT "Measure the float (soft FP64) datapath too" ON)
# The whole image runs from SRAM (the SDK's copy_to_ram binary type), as the
# ratchet's QEMU M33 runs from zero-wait-state memory: the cycles then
# measure the code, not the flash's XIP cache. OFF measures the default
# execute-in-place layout a deployment would ship.
option(PICO2_COPY_TO_RAM "Run the image from SRAM, matched to the ratchet (OFF: execute in place from flash)" ON)

add_executable(pico2_cyccnt main.cpp)
# Self-contained except for the header-only library itself.
# The family's three engines (bridge and rational for the synchronous cases;
# an engine's examples may reach a sibling, PLAN.md 4.2).
target_include_directories(pico2_cyccnt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../bridge/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../rational/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../submodules/dsptap/include)
target_link_libraries(pico2_cyccnt PRIVATE pico_stdlib cmsis_core)
if(PICO2_MEASURE_FLOAT)
    target_compile_definitions(pico2_cyccnt PRIVATE PICO2_MEASURE_FLOAT=1)
endif()

if(PICO2_COPY_TO_RAM)
    pico_set_binary_type(pico2_cyccnt copy_to_ram)
endif()

pico_enable_stdio_usb(pico2_cyccnt 1)
pico_enable_stdio_uart(pico2_cyccnt 0)
pico_add_extra_outputs(pico2_cyccnt) # .uf2 etc.
