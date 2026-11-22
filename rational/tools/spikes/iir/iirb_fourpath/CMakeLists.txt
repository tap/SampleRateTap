# IIR-b's four-path measurement (docs/IIR_PLAN.md I12; README.md here): a
# self-contained project, never part of the root build. Configure with the
# family's toolchain files, e.g.
#   cmake -S rational/tools/spikes/iir/iirb_fourpath -B build-fp-m33 -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake
#   cmake --build build-fp-m33 -j 4
#   python3 scripts/icount.py --engine rational --target m33 --build-dir build-fp-m33 \
#           --plugin libinsncount.so --baselines m33.json --update
# One binary per workload, format and profile, named like the ratchet's
# (tap_sr_rational_icount_*), so icount.py counts them as it counts those.
cmake_minimum_required(VERSION 3.24)
project(iirb_fourpath LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(_names pair_down four_down pair_up four_up x8_down_hb x8_down_fp x16_down_hb x16_down_fp x8_up_hb x8_up_fp
           x16_up_hb x16_up_fp construct)
set(_fmts float q15)
set(_profs se eco bal)
foreach(_fi RANGE 1)
    list(GET _fmts ${_fi} _f)
    foreach(_pi RANGE 2)
        list(GET _profs ${_pi} _p)
        foreach(_si RANGE 12)
            list(GET _names ${_si} _n)
            set(_t tap_sr_rational_icount_${_n}_${_f}_${_p})
            add_executable(${_t} fourpath_main.cpp)
            target_compile_definitions(${_t} PRIVATE FP_SC=${_si} FP_FMT=${_fi} FP_PROF=${_pi})
            # The measured draft of iir_stage.h first, then the engine's and
            # DspTap's headers.
            target_include_directories(${_t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${_root}/rational/include
                                                     ${_root}/submodules/dsptap/include)
        endforeach()
    endforeach()
endforeach()
