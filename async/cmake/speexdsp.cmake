# SpeexDSP's resampler (Xiph.Org, BSD-3) for the resampler comparison only
# (docs/COMPARISON.md): included by bench/compare, bench/icount
# (TAP_SR_ICOUNT_COMPARE) and tools/compare_shim so the host benchmark, the
# embedded counts and the notebook all measure the same pinned engine. Never
# linked into the library or its tests.
#
# It is the one competitor with a fixed-point build, so it is built twice from
# the same resample.c: FIXED_POINT (Q15 I/O, the build that can run on an
# FP64-less core on our Q15 row's terms) and FLOATING_POINT. The distro
# package is float-only, hence source. OUTSIDE_SPEEX is upstream's documented
# way to compile resample.c alone (stdlib allocation, no config.h); the
# RANDOM_PREFIX upstream insists on keeps the two builds' symbols apart.
#
# Pin: the commit the SpeexDSP-1.2.1 tag points at (tags can move; commits
# cannot). No CMake project upstream: fetch, don't add_subdirectory.
if(NOT TARGET tap_sr_async_speex_fixed)
    include(FetchContent)
    FetchContent_Declare(
        speexdsp
        GIT_REPOSITORY https://github.com/xiph/speexdsp.git
        GIT_TAG 1b28a0f61bc31162979e1f26f3981fc3637095c8 # SpeexDSP-1.2.1
        SOURCE_SUBDIR do-not-add)
    FetchContent_MakeAvailable(speexdsp)
    enable_language(C)

    foreach(_variant IN ITEMS fixed float)
        set(_lib tap_sr_async_speex_${_variant})
        add_library(${_lib} STATIC ${speexdsp_SOURCE_DIR}/libspeexdsp/resample.c)
        # SYSTEM: third-party headers stay out of our warning gates; the C
        # file itself is compiled without them (it is upstream's code).
        target_include_directories(${_lib} SYSTEM PUBLIC
            ${speexdsp_SOURCE_DIR}/include/speex ${speexdsp_SOURCE_DIR}/libspeexdsp)
        if(_variant STREQUAL fixed)
            set(_arith FIXED_POINT)
        else()
            set(_arith FLOATING_POINT)
        endif()
        # PRIVATE: a consumer defines the same OUTSIDE_SPEEX, arithmetic and
        # RANDOM_PREFIX before including speex_resampler.h (the shim and the
        # host bench include it once per build in one translation unit, so the
        # definitions cannot come from the targets).
        target_compile_definitions(${_lib} PRIVATE
            OUTSIDE_SPEEX ${_arith} RANDOM_PREFIX=tap_sr_cmp_${_variant} EXPORT=)
        set_target_properties(${_lib} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endforeach()
endif()
