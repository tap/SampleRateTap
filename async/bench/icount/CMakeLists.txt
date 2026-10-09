# One binary per scenario (name:kind:type:channels): bare-metal targets have
# no argv, and per-binary instruction totals are what the ratchet compares.
# construct_q15 (kind 2) is the Q15 pipeline with its stream removed —
# construction and the input fixture — so pipeline_q15 minus it is the
# streaming loop alone, which the pipeline total dilutes on soft-FP64 targets
# (docs/PERFORMANCE.md). Q15 stands for the three formats: their constructors
# differ only in the table's quantization (measured within 2 % of each other).
set(_srt_icount_scenarios
    kernel_float:0:0:2
    kernel_q15:0:1:2
    kernel_q31:0:2:2
    pipeline_float:1:0:2
    pipeline_q15:1:1:2
    pipeline_q31:1:2:2
    pipeline12_q15:1:1:12
    construct_q15:2:1:2)

foreach(_sc IN LISTS _srt_icount_scenarios)
    string(REPLACE ":" ";" _parts "${_sc}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _kind)
    list(GET _parts 2 _type)
    list(GET _parts 3 _ch)
    add_executable(tap_sr_async_icount_${_name} icount_main.cpp)
    target_compile_definitions(tap_sr_async_icount_${_name} PRIVATE
        TAP_SR_ASYNC_SC_KIND=${_kind} TAP_SR_ASYNC_SC_TYPE=${_type} TAP_SR_ASYNC_SC_CH=${_ch})
    target_link_libraries(tap_sr_async_icount_${_name} PRIVATE
        tap::sr::async tap_sr_async_warnings)
endforeach()

# Cross-resampler comparison workloads (docs/COMPARISON.md): same fixed
# workload through our datapath and through libsamplerate and r8brain, built for the
# same target. Named cmp_icount_* so the ratchet's tap_sr_async_icount_* glob never
# picks them up — competitor counts are recorded in docs, not gated.
option(TAP_SR_ICOUNT_COMPARE "Build resampler-comparison icount workloads" OFF)
if(TAP_SR_ICOUNT_COMPARE)
    # libsamplerate from its release tarball, for the target
    # (tools/compare/libsamplerate.cmake).
    include(${TAP_SR_COMPARE_TOOLS}/libsamplerate.cmake)

    # name:engine. Each engine is built twice, at the default 2 s and at 4 s
    # (suffix _4s): the difference is the steady-state cost, the remainder is
    # one-time construction (cmp_main.cpp's header).
    # name:engine[:quality] — the two SpeexDSP builds take the Speex quality
    # knob (0..10) as a third field: 10 is its best; 4 is its passband knee,
    # the lowest quality at the -0.42 dB it reaches at 20 kHz (the notebook's
    # sweep: no quality is flat to 20 kHz), at a quarter of quality 10's latency.
    set(_srt_cmp_engines srt_float:0 lsr_medium:1 lsr_best:2 r8b_120:3 r8b_120_tb8:4 srt_q15:5
        speex_fixed_q10:6:10 speex_float_q10:7:10 speex_fixed_q4:6:4 speex_float_q4:7:4)
    # r8brain-free-src, same pin as bench/compare. It compiles bare-metal only
    # with a no-op std::mutex supplied for thread-less newlib toolchains
    # (tools/compare/r8b_single_thread_mutex.h says why that is exact for
    # this workload).
    include(${TAP_SR_COMPARE_TOOLS}/r8brain.cmake)
    # SpeexDSP, the one competitor with a fixed-point build, compiled for the
    # target like the rest (tools/compare/speexdsp.cmake).
    include(${TAP_SR_COMPARE_TOOLS}/speexdsp.cmake)
    foreach(_e IN LISTS _srt_cmp_engines)
        string(REPLACE ":" ";" _parts "${_e}")
        list(GET _parts 0 _name)
        list(GET _parts 1 _eng)
        set(_quality 10)
        list(LENGTH _parts _n)
        if(_n GREATER 2)
            list(GET _parts 2 _quality)
        endif()
        foreach(_secs IN ITEMS 2 4)
            set(_bin cmp_icount_${_name})
            if(_secs EQUAL 4)
                set(_bin ${_bin}_4s)
            endif()
            add_executable(${_bin} cmp_main.cpp)
            target_compile_features(${_bin} PRIVATE cxx_std_20)
            target_compile_definitions(${_bin} PRIVATE TAP_SR_ASYNC_CMP_ENGINE=${_eng} TAP_SR_ASYNC_CMP_SECONDS=${_secs}
                TAP_SR_ASYNC_CMP_SPEEX_Q=${_quality})
            if(_eng EQUAL 0 OR _eng EQUAL 5)
                target_link_libraries(${_bin} PRIVATE tap::sr::async tap_sr_async_warnings)
            elseif(_eng LESS_EQUAL 2)
                # No tap_sr_async_warnings: samplerate.h is third-party.
                target_link_libraries(${_bin} PRIVATE samplerate)
            elseif(_eng EQUAL 6)
                # No tap_sr_async_warnings: the Speex headers are third-party.
                target_compile_definitions(${_bin} PRIVATE OUTSIDE_SPEEX FIXED_POINT RANDOM_PREFIX=tap_sr_cmp_fixed EXPORT=)
                target_link_libraries(${_bin} PRIVATE tap_sr_cmp_speex_fixed tap::sr::async)
            elseif(_eng EQUAL 7)
                target_compile_definitions(${_bin} PRIVATE OUTSIDE_SPEEX FLOATING_POINT RANDOM_PREFIX=tap_sr_cmp_float EXPORT=)
                # tap::sr::async for the shared requantize helper's header only.
                target_link_libraries(${_bin} PRIVATE tap_sr_cmp_speex_float tap::sr::async)
            else()
                target_compile_options(${_bin} PRIVATE -include ${TAP_SR_COMPARE_TOOLS}/r8b_single_thread_mutex.h)
                # No tap_sr_async_warnings: the r8brain headers are third-party.
                target_link_libraries(${_bin} PRIVATE tap_sr_cmp_r8brain)
            endif()
        endforeach()
    endforeach()
endif()
