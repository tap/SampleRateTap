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

# Cross-resampler comparison workloads (docs/COMPARISON.md): the same fixed
# task through bridge and through libsamplerate, r8brain and SpeexDSP, built
# for the same target, in both directions. Named cmp_bridge_icount_* so the
# ratchet's tap_sr_bridge_icount_* glob never picks them up — competitor
# counts are recorded in docs, not gated. The option is the family's
# (async/bench/icount declares it too; CMake tolerates the repeat).
option(TAP_SR_ICOUNT_COMPARE "Build resampler-comparison icount workloads" OFF)
if(TAP_SR_ICOUNT_COMPARE)
    include(${TAP_SR_COMPARE_TOOLS}/libsamplerate.cmake)
    include(${TAP_SR_COMPARE_TOOLS}/r8brain.cmake)
    include(${TAP_SR_COMPARE_TOOLS}/speexdsp.cmake)

    # name:engine[:quality], each built per direction (up_, down_) and at 2 s
    # and 4 s (suffix _4s): the difference is the steady-state cost, the
    # remainder is one-time construction (cmp_main.cpp's header, which also
    # says which competitor setting matches which tier).
    set(_bridge_cmp_engines
        srt_eco_float:0 srt_eco_q15:1 srt_tr_float:2
        lsr_medium:3 lsr_best:4
        r8b_eco:5 r8b_tr:6
        speex_fixed_q3:7:3 speex_float_q3:8:3 speex_float_q9:8:9)
    foreach(_dir IN ITEMS up down)
        if(_dir STREQUAL up)
            set(_dir_code 0)
        else()
            set(_dir_code 1)
        endif()
        foreach(_e IN LISTS _bridge_cmp_engines)
            string(REPLACE ":" ";" _parts "${_e}")
            list(GET _parts 0 _name)
            list(GET _parts 1 _eng)
            set(_quality 10)
            list(LENGTH _parts _n)
            if(_n GREATER 2)
                list(GET _parts 2 _quality)
            endif()
            foreach(_secs IN ITEMS 2 4)
                set(_bin cmp_bridge_icount_${_dir}_${_name})
                if(_secs EQUAL 4)
                    set(_bin ${_bin}_4s)
                endif()
                add_executable(${_bin} cmp_main.cpp)
                target_compile_features(${_bin} PRIVATE cxx_std_20)
                target_compile_definitions(${_bin} PRIVATE
                    TAP_SR_BRIDGE_CMP_DIR=${_dir_code} TAP_SR_BRIDGE_CMP_ENGINE=${_eng}
                    TAP_SR_BRIDGE_CMP_SECONDS=${_secs} TAP_SR_BRIDGE_CMP_SPEEX_Q=${_quality})
                # tap::dsp (through tap::sr::bridge) for the shared requantize
                # helper's header; the competitors' own headers are third-party,
                # so their binaries skip the warnings target.
                if(_eng LESS_EQUAL 2)
                    target_link_libraries(${_bin} PRIVATE tap::sr::bridge tap_sr_bridge_warnings)
                elseif(_eng LESS_EQUAL 4)
                    target_link_libraries(${_bin} PRIVATE samplerate tap::sr::bridge)
                elseif(_eng LESS_EQUAL 6)
                    target_compile_options(${_bin} PRIVATE -include ${TAP_SR_COMPARE_TOOLS}/r8b_single_thread_mutex.h)
                    target_link_libraries(${_bin} PRIVATE tap_sr_cmp_r8brain tap::sr::bridge)
                elseif(_eng EQUAL 7)
                    target_compile_definitions(${_bin} PRIVATE OUTSIDE_SPEEX FIXED_POINT RANDOM_PREFIX=tap_sr_cmp_fixed EXPORT=)
                    target_link_libraries(${_bin} PRIVATE tap_sr_cmp_speex_fixed tap::sr::bridge)
                else()
                    target_compile_definitions(${_bin} PRIVATE OUTSIDE_SPEEX FLOATING_POINT RANDOM_PREFIX=tap_sr_cmp_float EXPORT=)
                    target_link_libraries(${_bin} PRIVATE tap_sr_cmp_speex_float tap::sr::bridge)
                endif()
            endforeach()
        endforeach()
    endforeach()
endif()
