# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
#
# Interleaved against planar (informational, never a gate; scripts/layout.py).
# tap_sr_layout_<engine>_ch<C>_<i|p>: each binary includes and links one
# engine only (PLAN.md 4.2). One channel builds _i only.
set(_srt_layout_engines async:0 bridge:1 rational:2)
foreach(_e IN LISTS _srt_layout_engines)
    string(REPLACE ":" ";" _parts "${_e}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _id)
    set(_lib tap::sr::${_name})
    foreach(_ch IN ITEMS 1 2 6 12)
        set(_layouts i:0)
        if(_ch GREATER 1)
            list(APPEND _layouts p:1)
        endif()
        foreach(_l IN LISTS _layouts)
            string(REPLACE ":" ";" _lp "${_l}")
            list(GET _lp 0 _lname)
            list(GET _lp 1 _planar)
            set(_bin tap_sr_layout_${_name}_ch${_ch}_${_lname})
            add_executable(${_bin} layout_main.cpp)
            target_compile_definitions(${_bin} PRIVATE
                TAP_SR_LY_ENGINE=${_id} TAP_SR_LY_CH=${_ch} TAP_SR_LY_PLANAR=${_planar})
            target_include_directories(${_bin} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../probe)
            target_link_libraries(${_bin} PRIVATE ${_lib})
        endforeach()
    endforeach()
endforeach()
