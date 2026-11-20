# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
#
# IIR-0, go/no-go 6 (IIR_PLAN I4), for a host this spike could not run on
# (MSVC on Windows, AppleClang on macOS): a self-contained project, no
# dependency beyond CMake 3.20 and a C++20 compiler. Not part of any build
# CI runs today; a job that wants the answer runs, from this folder:
#
#   cmake -S . -B build && cmake --build build --config Release
#   ctest --test-dir build -C Release --output-on-failure
#       # cx.BitsMatchGccAndClang: the printed tables equal expected_bits.txt
#       #   (GCC 13 and Clang 18 on Linux x86-64, bit for bit)
#   cmake -DWHICH=1 -P probe_steps.cmake   # the step margin of one design
#       # (1: the N17 half-band, the largest named order; 3: the order-11
#       #  Chebyshev II with the L-inf sweep; 5: the same with unity DC
#       #  gains; see designs_main.cpp)
#
# IIR_CX_WHICH picks the designs evaluated (0 = all), IIR_CX_STEPS a step
# limit in the compiler's own units (MSVC /constexpr:steps, Clang
# -fconstexpr-steps, GCC -fconstexpr-ops-limit); empty keeps the default.
cmake_minimum_required(VERSION 3.20)
project(iir_spike_constexpr CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IIR_CX_WHICH 0 CACHE STRING "designs evaluated at compile time (designs_main.cpp)")
set(IIR_CX_STEPS "" CACHE STRING "constexpr step limit, in the compiler's units; empty: its default")

add_executable(cx_designs designs_main.cpp)
target_compile_definitions(cx_designs PRIVATE IIR_CX_WHICH=${IIR_CX_WHICH})
if(MSVC)
    # the family's Windows leg builds with /W4 /WX
    target_compile_options(cx_designs PRIVATE /W4 /WX /permissive-)
    if(IIR_CX_STEPS)
        target_compile_options(cx_designs PRIVATE /constexpr:steps${IIR_CX_STEPS})
    endif()
else()
    target_compile_options(cx_designs PRIVATE -Wall -Wextra -Werror)
    if(IIR_CX_STEPS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(cx_designs PRIVATE -fconstexpr-steps=${IIR_CX_STEPS})
        else()
            target_compile_options(cx_designs PRIVATE -fconstexpr-ops-limit=${IIR_CX_STEPS})
        endif()
    endif()
endif()

enable_testing()
if(IIR_CX_WHICH EQUAL 0)
    add_test(NAME cx.BitsMatchGccAndClang
             COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:cx_designs>
                     -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected_bits.txt
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/compare_bits.cmake)
endif()
