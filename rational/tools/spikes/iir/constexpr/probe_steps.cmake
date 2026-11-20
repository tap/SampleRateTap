# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
#
# cmake -DWHICH=<1..5> [-DGENERATOR=...] -P probe_steps.cmake
#
# The smallest constexpr step limit (in the compiler's own units) under which
# one design of designs_main.cpp compiles, by bisection over configure +
# build of this folder's project, with whatever compiler CMake picks on this
# host (MSVC on Windows). Then the margin against the compiler's default:
# MSVC /constexpr:steps 100000 (its documented default), Clang 1048576,
# GCC -fconstexpr-ops-limit 33554432.
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED WHICH)
    set(WHICH 1)
endif()
get_filename_component(src "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(work "${CMAKE_CURRENT_BINARY_DIR}/probe-steps-${WHICH}")
set(gen_args "")
if(DEFINED GENERATOR)
    set(gen_args -G "${GENERATOR}")
endif()

function(try_limit limit out)
    file(REMOVE_RECURSE "${work}")
    execute_process(COMMAND ${CMAKE_COMMAND} -S "${src}" -B "${work}" ${gen_args}
                            -DIIR_CX_WHICH=${WHICH} -DIIR_CX_STEPS=${limit}
                    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
    if(rc EQUAL 0)
        execute_process(COMMAND ${CMAKE_COMMAND} --build "${work}" --config Release
                        RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
    endif()
    if(rc EQUAL 0)
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# the default first: does it compile at all?
file(REMOVE_RECURSE "${work}")
execute_process(COMMAND ${CMAKE_COMMAND} -S "${src}" -B "${work}" ${gen_args} -DIIR_CX_WHICH=${WHICH} RESULT_VARIABLE rc
                OUTPUT_QUIET ERROR_QUIET)
execute_process(COMMAND ${CMAKE_COMMAND} --build "${work}" --config Release RESULT_VARIABLE rc OUTPUT_VARIABLE log
                ERROR_VARIABLE log)
if(NOT rc EQUAL 0)
    message("design ${WHICH}: does NOT compile under the compiler's default limit:\n${log}")
endif()

set(lo 1)
set(hi 1024)
while(TRUE)
    try_limit(${hi} ok)
    if(ok)
        break()
    endif()
    set(lo ${hi})
    math(EXPR hi "${hi} * 2")
    if(hi GREATER 1073741824)
        message(FATAL_ERROR "design ${WHICH}: no limit up to 2^30 compiles")
    endif()
endwhile()
while(TRUE)
    math(EXPR span "${hi} - ${lo}")
    if(span LESS_EQUAL 1)
        break()
    endif()
    math(EXPR mid "(${lo} + ${hi}) / 2")
    try_limit(${mid} ok)
    if(ok)
        set(hi ${mid})
    else()
        set(lo ${mid})
    endif()
endwhile()
message("design ${WHICH}: needs a constexpr limit of ${hi} (the compiler's own units)")
