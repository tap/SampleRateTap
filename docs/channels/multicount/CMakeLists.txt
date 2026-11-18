# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
#
# Gate 0's multi-count probe (docs/CHANNELS_PLAN.md section 3, item 3): one
# rational up_2 and one down_2 stage, run at each count of a set in one object,
# with the reference's headers (TODAY: 2ed199a's, the count at run time) and a
# variant tree's, compiled by a target's own toolchain file and measured as the
# object's .text. VARIANT_API names the variant's call shape: `a13` (the
# default) is A13's, basic_stage<S, Ch, R> constructed without a count, as
# every A13 PR re-runs it; `prototype` is gate 0's K12 prototype from
# make_variant.py, basic_stage<S, R, Ch> constructed with it. The source is
# written at configure time, so the tree carries no C++ outside the engines.
#
#   cmake -S docs/channels/multicount -B <dir> -DCMAKE_BUILD_TYPE=Release|MinSizeRel
#         -DCMAKE_TOOLCHAIN_FILE=<tree>/cmake/<target>.cmake -DTODAY=<reference>
#         -DVARIANT_TREE=<variant> [-DVARIANT_API=a13|prototype]
cmake_minimum_required(VERSION 3.24)
project(gate0_multicount CXX)
set(CMAKE_CXX_STANDARD 20)
set(VARIANT_API a13 CACHE STRING "The variant tree's call shape: a13 or prototype")
file(WRITE ${CMAKE_BINARY_DIR}/probe.cpp [=[
#include <cstddef>
#include <cstdint>
#include "tap/sr/rational/stage.h"
#ifndef PT
#define PT float
#endif
using sample = PT;
namespace rat = tap::sr::rational;
#if VARIANT == 2
template <std::size_t C> using up_t = rat::basic_stage<sample, C, rat::up_2>;
template <std::size_t C> using dn_t = rat::basic_stage<sample, C, rat::down_2>;
#elif VARIANT == 1
template <std::size_t C> using up_t = rat::basic_stage<sample, rat::up_2, C>;
template <std::size_t C> using dn_t = rat::basic_stage<sample, rat::down_2, C>;
#else
template <std::size_t C> using up_t = rat::basic_stage<sample, rat::up_2>;
template <std::size_t C> using dn_t = rat::basic_stage<sample, rat::down_2>;
#endif
sample g_in[64 * 2], g_out[200 * 2];
template <std::size_t C> [[gnu::noinline]] std::size_t run() {
#if VARIANT == 2
    up_t<C> up; dn_t<C> dn; std::size_t m = 0;
#else
    up_t<C> up(C); dn_t<C> dn(C); std::size_t m = 0;
#endif
    for (int b = 0; b < 100; ++b) { m += up.process(g_in, 32, g_out); m += dn.process(g_in, 32, g_out); }
    return m;
}
extern "C" int probe_entry() { return static_cast<int>((run<1>() NCOUNTS) & 1U); }
]=])
foreach(tree IN ITEMS today k12)
  foreach(fmt IN ITEMS float int16)
    foreach(set IN ITEMS c1 c12 c126)
      set(t p_${tree}_${fmt}_${set})
      add_library(${t} OBJECT ${CMAKE_BINARY_DIR}/probe.cpp)
      if(tree STREQUAL today)
        target_include_directories(${t} PRIVATE ${TODAY}/rational/include ${TODAY}/submodules/dsptap/include)
        target_compile_definitions(${t} PRIVATE VARIANT=0)
      else()
        target_include_directories(${t} PRIVATE ${VARIANT_TREE}/rational/include ${VARIANT_TREE}/submodules/dsptap/include)
        if(VARIANT_API STREQUAL "prototype")
          target_compile_definitions(${t} PRIVATE VARIANT=1)
        else()
          target_compile_definitions(${t} PRIVATE VARIANT=2)
        endif()
      endif()
      if(fmt STREQUAL float)
        target_compile_definitions(${t} PRIVATE PT=float)
      else()
        target_compile_definitions(${t} PRIVATE PT=std::int16_t)
      endif()
      if(set STREQUAL c1)
        target_compile_definitions(${t} PRIVATE "NCOUNTS=")
      elseif(set STREQUAL c12)
        target_compile_definitions(${t} PRIVATE "NCOUNTS=+ run<2>()")
      else()
        target_compile_definitions(${t} PRIVATE "NCOUNTS=+ run<2>() + run<6>()")
      endif()
      target_compile_options(${t} PRIVATE -ffunction-sections)
    endforeach()
  endforeach()
endforeach()
