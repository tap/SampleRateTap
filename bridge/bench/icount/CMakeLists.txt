# One binary per scenario (name:dir:type:profile:channels): bare-metal
# targets have no argv, and per-binary instruction totals are what the
# ratchet compares. Economy is the speed-first default the M7 levers are
# judged on, in every format and both directions; the two transparent legs
# keep the pristine profile honest without tripling the matrix, and the two
# Q15 super_economy legs pin the voice tier in its deployment format. The two
# construct_* legs (a sixth field, kind 1) are the Q15 economy workload with
# its stream removed — construction and the input fixture — one per direction
# since the two prototypes differ: stream minus construct is the streaming
# loop alone, which the Q15 totals dilute on M33 (PLAN.md section 7, M7e).
set(_ratio_icount_scenarios
    up_float_eco:0:0:0:2
    down_float_eco:1:0:0:2
    up_q15_eco:0:1:0:2
    down_q15_eco:1:1:0:2
    up_q31_eco:0:2:0:2
    down_q31_eco:1:2:0:2
    up_float_tr:0:0:1:2
    down_float_tr:1:0:1:2
    up_q15_se:0:1:3:2
    down_q15_se:1:1:3:2
    construct_up_q15_eco:0:1:0:2:1
    construct_down_q15_eco:1:1:0:2:1)

foreach(_sc IN LISTS _ratio_icount_scenarios)
    string(REPLACE ":" ";" _parts "${_sc}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _dir)
    list(GET _parts 2 _type)
    list(GET _parts 3 _profile)
    list(GET _parts 4 _ch)
    set(_kind 0)
    list(LENGTH _parts _n)
    if(_n GREATER 5)
        list(GET _parts 5 _kind)
    endif()
    add_executable(tap_sr_bridge_icount_${_name} icount_main.cpp)
    target_compile_definitions(tap_sr_bridge_icount_${_name} PRIVATE
        TAP_SR_BRIDGE_SC_DIR=${_dir} TAP_SR_BRIDGE_SC_TYPE=${_type}
        TAP_SR_BRIDGE_SC_PROFILE=${_profile} TAP_SR_BRIDGE_SC_CH=${_ch}
        TAP_SR_BRIDGE_SC_KIND=${_kind})
    target_link_libraries(tap_sr_bridge_icount_${_name} PRIVATE
        tap::sr::bridge tap_sr_bridge_warnings)
endforeach()
