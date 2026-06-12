# One binary per scenario (name:kind:type:channels): bare-metal targets have
# no argv, and per-binary instruction totals are what the ratchet compares.
set(_srt_icount_scenarios
    kernel_float:0:0:2
    kernel_q15:0:1:2
    kernel_q31:0:2:2
    pipeline_float:1:0:2
    pipeline_q15:1:1:2
    pipeline_q31:1:2:2
    pipeline12_q15:1:1:12)

foreach(_sc IN LISTS _srt_icount_scenarios)
    string(REPLACE ":" ";" _parts "${_sc}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _kind)
    list(GET _parts 2 _type)
    list(GET _parts 3 _ch)
    add_executable(srt_icount_${_name} icount_main.cpp)
    target_compile_definitions(srt_icount_${_name} PRIVATE
        SRT_SC_KIND=${_kind} SRT_SC_TYPE=${_type} SRT_SC_CH=${_ch})
    target_link_libraries(srt_icount_${_name} PRIVATE
        SampleRateTap::SampleRateTap srt_warnings)
endforeach()

# Cross-resampler comparison workloads (docs/COMPARISON.md): same fixed
# workload through our datapath and through libsamplerate, built for the
# same target. Named cmp_icount_* so the ratchet's srt_icount_* glob never
# picks them up — competitor counts are recorded in docs, not gated.
option(SRT_ICOUNT_COMPARE "Build resampler-comparison icount workloads" OFF)
if(SRT_ICOUNT_COMPARE)
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LIBSAMPLERATE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libsamplerate
        URL https://github.com/libsndfile/libsamplerate/releases/download/0.2.2/libsamplerate-0.2.2.tar.xz
        URL_HASH SHA256=3258da280511d24b49d6b08615bbe824d0cacc9842b0e4caf11c52cf2b043893)
    FetchContent_MakeAvailable(libsamplerate)

    add_executable(cmp_icount_srt_float cmp_main.cpp)
    target_compile_definitions(cmp_icount_srt_float PRIVATE SRT_CMP_ENGINE=0)
    target_link_libraries(cmp_icount_srt_float PRIVATE
        SampleRateTap::SampleRateTap srt_warnings)

    foreach(_eng IN ITEMS 1 2)
        if(_eng EQUAL 1)
            set(_name lsr_medium)
        else()
            set(_name lsr_best)
        endif()
        add_executable(cmp_icount_${_name} cmp_main.cpp)
        target_compile_features(cmp_icount_${_name} PRIVATE cxx_std_20)
        target_compile_definitions(cmp_icount_${_name} PRIVATE SRT_CMP_ENGINE=${_eng})
        # No srt_warnings: samplerate.h is third-party.
        target_link_libraries(cmp_icount_${_name} PRIVATE samplerate)
    endforeach()
endif()
