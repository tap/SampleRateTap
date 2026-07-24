# One binary per scenario (name:dir:type:profile:channels): bare-metal
# targets have no argv, and per-binary instruction totals are what the
# ratchet compares. Economy is the speed-first default the M7 levers are
# judged on, in every format and both directions; the two transparent legs
# keep the pristine profile honest without tripling the matrix.
set(_ratio_icount_scenarios
    up_float_eco:0:0:0:2
    down_float_eco:1:0:0:2
    up_q15_eco:0:1:0:2
    down_q15_eco:1:1:0:2
    up_q31_eco:0:2:0:2
    down_q31_eco:1:2:0:2
    up_float_tr:0:0:1:2
    down_float_tr:1:0:1:2)

foreach(_sc IN LISTS _ratio_icount_scenarios)
    string(REPLACE ":" ";" _parts "${_sc}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _dir)
    list(GET _parts 2 _type)
    list(GET _parts 3 _profile)
    list(GET _parts 4 _ch)
    add_executable(ratio_icount_${_name} icount_main.cpp)
    target_compile_definitions(ratio_icount_${_name} PRIVATE
        RATIO_SC_DIR=${_dir} RATIO_SC_TYPE=${_type}
        RATIO_SC_PROFILE=${_profile} RATIO_SC_CH=${_ch})
    target_link_libraries(ratio_icount_${_name} PRIVATE
        tap::ratio tap_ratio_warnings)
endforeach()
