# Cross-compilation toolchain for Arm Cortex-M55, bare metal (newlib +
# semihosting), executed on QEMU's MPS3 AN547 board model. Ported from
# SampleRateTap's cmake/ (the family's embedded targets share one story).
#
# Usage:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
#         -DTAP_RATIO_BUILD_EXAMPLES=OFF ...
# with arm-none-eabi-g++ and qemu-system-arm on PATH.
#
# Notes:
#  - Bare metal: no std::thread (the test build adapts; see
#    tests/CMakeLists.txt and TAP_RATIO_BARE_METAL below).
#  - The M55 FPU has no double precision, so the double-typed design path
#    (table construction) runs soft-float here: correctness coverage, not a
#    performance measurement. The float/Q15/Q31 process loops are hard-FP.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m55 -mthumb -mfloat-abi=hard -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

get_filename_component(_tap_platform "${CMAKE_CURRENT_LIST_DIR}/../platform/mps3_an547" ABSOLUTE)
# The startup .c is handed to the link line directly; the gcc driver
# compiles it with the same -mcpu/-mfloat-abi flags as everything else.
# `-x c` forces C compilation even under the g++ driver (which would treat
# a .c link input as C++): C guarantees the vector table's address-constant
# initializers are link-time constants, never dynamic initialization.
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "--specs=rdimon.specs -nostartfiles -Wl,--gc-sections -T${_tap_platform}/mps3_an547.ld -x c ${CMAKE_CURRENT_LIST_DIR}/../platform/armv8m_startup.c -x none")

set(CMAKE_CROSSCOMPILING_EMULATOR
    "qemu-system-arm;-M;mps3-an547;-nographic;-semihosting;-kernel")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Switches the test harness to one-shot mode: a single registered CTest test
# running the whole (emulation-sized) suite, judged by gtest's summary text
# rather than the exit code, which semihosting does not reliably propagate.
set(TAP_RATIO_BARE_METAL ON)
