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
# workload through our datapath and through libsamplerate and r8brain, built for the
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

    # name:engine. Each engine is built twice, at the default 2 s and at 4 s
    # (suffix _4s): the difference is the steady-state cost, the remainder is
    # one-time construction (cmp_main.cpp's header).
    set(_srt_cmp_engines srt_float:0 lsr_medium:1 lsr_best:2 r8b_120:3 r8b_120_tb8:4 srt_q15:5)
    # r8brain-free-src, same pin as bench/compare. It compiles bare-metal only
    # with a no-op std::mutex supplied for thread-less newlib toolchains
    # (r8b_single_thread_mutex.h says why that is exact for this workload).
    include(${PROJECT_SOURCE_DIR}/cmake/r8brain.cmake)
    foreach(_e IN LISTS _srt_cmp_engines)
        string(REPLACE ":" ";" _parts "${_e}")
        list(GET _parts 0 _name)
        list(GET _parts 1 _eng)
        foreach(_secs IN ITEMS 2 4)
            set(_bin cmp_icount_${_name})
            if(_secs EQUAL 4)
                set(_bin ${_bin}_4s)
            endif()
            add_executable(${_bin} cmp_main.cpp)
            target_compile_features(${_bin} PRIVATE cxx_std_20)
            target_compile_definitions(${_bin} PRIVATE SRT_CMP_ENGINE=${_eng} SRT_CMP_SECONDS=${_secs})
            if(_eng EQUAL 0 OR _eng EQUAL 5)
                target_link_libraries(${_bin} PRIVATE SampleRateTap::SampleRateTap srt_warnings)
            elseif(_eng LESS_EQUAL 2)
                # No srt_warnings: samplerate.h is third-party.
                target_link_libraries(${_bin} PRIVATE samplerate)
            else()
                target_compile_options(${_bin} PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/r8b_single_thread_mutex.h)
                # No srt_warnings: the r8brain headers are third-party.
                target_link_libraries(${_bin} PRIVATE srt_r8brain)
            endif()
        endforeach()
    endforeach()
endif()
