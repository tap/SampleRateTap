# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
#
# cmake -DEXE=<cx_designs> -DEXPECTED=<expected_bits.txt> -P compare_bits.cmake
# Runs the designs and compares each table's bits with the expected file,
# ignoring the libm column (each platform's libm differs) and line endings.
execute_process(COMMAND ${EXE} OUTPUT_VARIABLE got RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "cx_designs exited ${rc}")
endif()
file(READ ${EXPECTED} want)
foreach(v got want)
    string(REPLACE "\r" "" ${v} "${${v}}")
    string(REGEX REPLACE " \\| libm [^\n]*" "" ${v} "${${v}}")
endforeach()
if(NOT got STREQUAL want)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/got_bits.txt "${got}")
    message(FATAL_ERROR "the constexpr tables differ from GCC 13 / Clang 18's (got_bits.txt beside the build)")
endif()
message(STATUS "every table matches expected_bits.txt bit for bit")
