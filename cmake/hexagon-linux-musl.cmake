# Cross-compilation toolchain for Qualcomm Hexagon (Linux/musl), using the
# open-source toolchain from https://github.com/quic/toolchain_for_hexagon
# with tests executed under qemu-hexagon user-mode emulation.
#
# Usage:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/hexagon-linux-musl.cmake ...
# with hexagon-unknown-linux-musl-clang++ and qemu-hexagon on PATH.
#
# Note: emulation validates ISA-level *correctness* (32-bit size_t, atomics
# lowering, musl libc), not performance. Hexagon has no double-precision
# FPU, so the double-heavy paths run soft-float. Cycle counts need the
# Hexagon SDK simulator.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR hexagon)

set(CMAKE_C_COMPILER hexagon-unknown-linux-musl-clang)
set(CMAKE_CXX_COMPILER hexagon-unknown-linux-musl-clang++)

# Static linking so the emulator needs no sysroot/loader configuration.
# --eh-frame-hdr: the clang driver passes it for dynamic links only, and
# without the PT_GNU_EH_FRAME segment it builds, libunwind finds no unwind
# tables in a static image, so every C++ throw terminated (libc++abi)
# instead of reaching its catch. With it, exceptions propagate under
# qemu-hexagon (async/docs/PERFORMANCE.md, "Hexagon exceptions").
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -Wl,--eh-frame-hdr")

set(CMAKE_CROSSCOMPILING_EMULATOR qemu-hexagon)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
