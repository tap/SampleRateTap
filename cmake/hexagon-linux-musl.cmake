# Cross-compilation toolchain for Qualcomm Hexagon (Linux/musl), using the
# open-source toolchain from https://github.com/quic/toolchain_for_hexagon
# with tests executed under qemu-hexagon user-mode emulation.
#
# Usage:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/hexagon-linux-musl.cmake ...
# with hexagon-unknown-linux-musl-clang++ and qemu-hexagon on PATH.
#
# Note: emulation validates ISA-level *correctness* (32-bit size_t, atomics
# lowering, musl libc), not performance. Caveat: under this static-musl
# configuration C++ exceptions terminate (libc++abi) instead of
# propagating — constructor validation errors are fatal here; see the
# Known-debt entry in docs/PERFORMANCE.md — Hexagon has no double-precision
# FPU, so the double-heavy paths run soft-float. Cycle counts need the
# Hexagon SDK simulator.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR hexagon)

set(CMAKE_C_COMPILER hexagon-unknown-linux-musl-clang)
set(CMAKE_CXX_COMPILER hexagon-unknown-linux-musl-clang++)

# Static linking so the emulator needs no sysroot/loader configuration.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

set(CMAKE_CROSSCOMPILING_EMULATOR qemu-hexagon)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
