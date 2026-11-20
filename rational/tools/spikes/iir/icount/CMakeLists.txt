# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
#
# IIR-0's scratch target build (not part of any build CI runs): the counted
# workloads (workloads.cpp, one binary per scenario) and the FTZ check
# (../ftz/ftz_check.cpp), for the host and, through the family's toolchain
# files, the Cortex-M4 / M4F (DspTap's), M33, M55 and Hexagon. Configure it
# on its own; run_targets.py configures, builds and runs every target:
#
#   cmake -S rational/tools/spikes/iir/icount -B build-iir-m33 \
#         -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake
cmake_minimum_required(VERSION 3.20)
project(iir_spike_targets CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(_repo "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(_inc ${CMAKE_CURRENT_SOURCE_DIR}/../cpp ${CMAKE_CURRENT_SOURCE_DIR}/../ftz ${_repo}/tools/probe)

# name:id, as workloads.cpp's IIR_SC switch
set(_scenarios
    dec_float:0 dec_double:1 dec_q15:2 dec_q31:3 dec_float_flush:4 dec_double_flush:5
    dec_float_guard_hostoff:6 dec_float_guard_hoston:7 guard_only_hostoff:8 guard_only_hoston:9
    nr_dec_float:10 nr_dec_q15:11 int_float:12 int_q15:13
    tr_dec_float:14 tr_dec_double:15 tr_dec_float_flush:16 tr_dec_double_flush:17
    dec_q15_floor:18 dec_q31_floor:19 guard_none:40 dec_float_wrapper:41
    dec_float_callflush:42 dec_float_offset:43)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../cpp/fourpath_designs.h)
    list(APPEND _scenarios
         x4_hb_float:20 x4_fp_float:21 x8_hb_float:22 x8_fp_float:23 x16_hb_float:24 x16_fp_float:25 x16_fptop_float:26
         x4_hb_q15:30 x4_fp_q15:31 x8_hb_q15:32 x8_fp_q15:33 x16_hb_q15:34 x16_fp_q15:35 x16_fptop_q15:36)
    set(_fp ON)
endif()

foreach(_sc IN LISTS _scenarios)
    string(REPLACE ":" ";" _parts "${_sc}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _id)
    add_executable(iir_w_${_name} workloads.cpp)
    target_compile_definitions(iir_w_${_name} PRIVATE IIR_SC=${_id} $<$<BOOL:${_fp}>:IIR_HAVE_FOURPATH>)
    target_include_directories(iir_w_${_name} PRIVATE ${_inc} ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()

add_executable(iir_ftz_check ../ftz/ftz_check.cpp)
target_include_directories(iir_ftz_check PRIVATE ${_inc})
