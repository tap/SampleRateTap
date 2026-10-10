# One binary per scenario (name:id): bare-metal targets have no argv, and
# per-binary instruction totals are what the ratchet compares
# (scripts/icount.py --engine rational). The workloads are PLAN.md section
# 5's: the by-2 and by-3 stages both ways in float and Q15 at economy, the
# by-4 chain (down 2 . down 2) in Q15, the by-2 pair in float at
# transparent, and the Q15 by-4 chain's workload with its stream removed
# (construction and the input fixture, then one block: stream minus it is
# the streaming loop alone, as the siblings' construct legs) as its own
# scenario from the first baseline, the fixture joining it 2026-10-10; the
# mixed ratio 2/3 (48 -> 32 kHz) in float and Q15 at economy joined after
# M6, when the sparse-row lever was measured on it (PLAN.md section 6).
set(_rational_icount_scenarios
    up2_float_eco:0
    down2_float_eco:1
    up3_float_eco:2
    down3_float_eco:3
    up2_q15_eco:4
    down2_q15_eco:5
    up3_q15_eco:6
    down3_q15_eco:7
    down2_down2_q15_eco:8
    up2_float_tr:9
    down2_float_tr:10
    construct_q15_eco:11
    ratio23_float_eco:12
    ratio23_q15_eco:13)

foreach(_sc IN LISTS _rational_icount_scenarios)
    string(REPLACE ":" ";" _parts "${_sc}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _id)
    add_executable(tap_sr_rational_icount_${_name} icount_main.cpp)
    target_compile_definitions(tap_sr_rational_icount_${_name} PRIVATE TAP_SR_RATIONAL_SC=${_id})
    target_link_libraries(tap_sr_rational_icount_${_name} PRIVATE tap::sr::rational tap_sr_rational_warnings)
endforeach()

# Cross-resampler comparison workloads (docs/COMPARISON.md): the same fixed
# task through rational and through libsamplerate, r8brain and SpeexDSP,
# built for the same target, at four ratios of the vocabulary. Named
# cmp_rational_icount_* so the ratchet's tap_sr_rational_icount_* glob never
# picks them up — competitor counts are recorded in docs, not gated. The
# option is the family's (the siblings' bench/icount declare it too).
option(TAP_SR_ICOUNT_COMPARE "Build resampler-comparison icount workloads" OFF)
if(TAP_SR_ICOUNT_COMPARE)
    include(${TAP_SR_COMPARE_TOOLS}/libsamplerate.cmake)
    include(${TAP_SR_COMPARE_TOOLS}/r8brain.cmake)
    include(${TAP_SR_COMPARE_TOOLS}/speexdsp.cmake)

    # name:engine[:quality], each built per ratio (up2_, down2_, r32_, r23_)
    # and at 2 s and 4 s (suffix _4s): the difference is the steady-state
    # cost, the remainder is one-time construction (cmp_main.cpp's header,
    # which also says which competitor setting matches which tier).
    set(_rational_cmp_engines
        srt_eco_float:0 srt_eco_q15:1 srt_tr_float:2
        lsr_medium:3 lsr_best:4
        r8b_eco:5 r8b_tr:6
        speex_fixed_q3:7:3 speex_float_q2:8:2 speex_float_q9:8:9)
    set(_rational_cmp_ratios up2:0 down2:1 r32:2 r23:3)
    foreach(_r IN LISTS _rational_cmp_ratios)
        string(REPLACE ":" ";" _rparts "${_r}")
        list(GET _rparts 0 _rname)
        list(GET _rparts 1 _rcode)
        foreach(_e IN LISTS _rational_cmp_engines)
            string(REPLACE ":" ";" _parts "${_e}")
            list(GET _parts 0 _name)
            list(GET _parts 1 _eng)
            set(_quality 10)
            list(LENGTH _parts _n)
            if(_n GREATER 2)
                list(GET _parts 2 _quality)
            endif()
            foreach(_secs IN ITEMS 2 4)
                set(_bin cmp_rational_icount_${_rname}_${_name})
                if(_secs EQUAL 4)
                    set(_bin ${_bin}_4s)
                endif()
                add_executable(${_bin} cmp_main.cpp)
                target_compile_features(${_bin} PRIVATE cxx_std_20)
                target_compile_definitions(${_bin} PRIVATE
                    TAP_SR_RATIONAL_CMP_RATIO=${_rcode} TAP_SR_RATIONAL_CMP_ENGINE=${_eng}
                    TAP_SR_RATIONAL_CMP_SECONDS=${_secs} TAP_SR_RATIONAL_CMP_SPEEX_Q=${_quality})
                # tap::dsp (through tap::sr::rational) for the shared requantize
                # helper's header; the competitors' own headers are third-party,
                # so their binaries skip the warnings target.
                if(_eng LESS_EQUAL 2)
                    target_link_libraries(${_bin} PRIVATE tap::sr::rational tap_sr_rational_warnings)
                elseif(_eng LESS_EQUAL 4)
                    target_link_libraries(${_bin} PRIVATE samplerate tap::sr::rational)
                elseif(_eng LESS_EQUAL 6)
                    target_compile_options(${_bin} PRIVATE -include ${TAP_SR_COMPARE_TOOLS}/r8b_single_thread_mutex.h)
                    target_link_libraries(${_bin} PRIVATE tap_sr_cmp_r8brain tap::sr::rational)
                elseif(_eng EQUAL 7)
                    target_compile_definitions(${_bin} PRIVATE OUTSIDE_SPEEX FIXED_POINT RANDOM_PREFIX=tap_sr_cmp_fixed EXPORT=)
                    target_link_libraries(${_bin} PRIVATE tap_sr_cmp_speex_fixed tap::sr::rational)
                else()
                    target_compile_definitions(${_bin} PRIVATE OUTSIDE_SPEEX FLOATING_POINT RANDOM_PREFIX=tap_sr_cmp_float EXPORT=)
                    target_link_libraries(${_bin} PRIVATE tap_sr_cmp_speex_float tap::sr::rational)
                endif()
            endforeach()
        endforeach()
    endforeach()
endif()
