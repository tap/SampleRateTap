# Cross-compilation toolchain for Arm Cortex-M33 (bare metal, newlib +
# semihosting), executed on QEMU's MPS2+ AN505 board model. This is the
# Raspberry Pi Pico 2 (RP2350) class of core: single-precision FPU only, so
# all double arithmetic is soft-float — the fixed-point Q15/Q31 datapaths
# are the intended formats here, and the icount baselines quantify exactly
# what the float path costs. Ported from SampleRateTap's cmake/.
#
# Usage:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
#         -DTAP_RATIO_BUILD_EXAMPLES=OFF ...
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

get_filename_component(_tap_platform "${CMAKE_CURRENT_LIST_DIR}/../platform" ABSOLUTE)
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "--specs=rdimon.specs -nostartfiles -Wl,--gc-sections -T${_tap_platform}/mps2_an505/mps2_an505.ld -x c ${_tap_platform}/armv8m_startup.c -x none")

set(CMAKE_CROSSCOMPILING_EMULATOR
    "qemu-system-arm;-M;mps2-an505;-nographic;-semihosting;-kernel")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# One-shot CTest mode (no argv on bare metal; see tests/CMakeLists.txt).
set(TAP_RATIO_BARE_METAL ON)
