# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
# Spike X-0's x8 workloads (icount_x8.cpp), one binary per chain, format and
# stream length; named x0_icount_*, apart from the ratchet's
# tap_sr_rational_icount_* glob. Included only by hook.cmake.
foreach(_chain IN ITEMS down4_down2:0 down3_ratio34_down2:1 down2_down2_down2:2 ratio38_down3:3
                        up2_up4:4 up2_ratio43_up3:5 up2_up2_up2:6 up3_ratio83:7)
    string(REPLACE ":" ";" _parts "${_chain}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _id)
    foreach(_fmt IN ITEMS float:0 q15:1)
        string(REPLACE ":" ";" _fparts "${_fmt}")
        list(GET _fparts 0 _fname)
        list(GET _fparts 1 _q15)
        foreach(_blocks IN ITEMS 1000 3000)
            set(_t x0_icount_${_name}_${_fname}_${_blocks})
            add_executable(${_t} ${CMAKE_CURRENT_LIST_DIR}/icount_x8.cpp)
            target_compile_definitions(${_t} PRIVATE X0_CHAIN=${_id} X0_Q15=${_q15} X0_BLOCKS=${_blocks})
            target_link_libraries(${_t} PRIVATE tap::sr::rational tap_sr_rational_warnings)
        endforeach()
    endforeach()
endforeach()
