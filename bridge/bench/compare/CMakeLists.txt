# Head-to-head computational comparison against general-purpose resamplers
# (libsamplerate, soxr, r8brain-free-src, SpeexDSP) at the 44.1 <-> 48 pair,
# both directions. Host-only: libsamplerate and soxr come from the system
# (pkg-config); r8brain (header-only) and SpeexDSP's resample.c (built twice,
# fixed and float) are fetched at commit pins by the family's comparison
# tooling (tools/compare/r8brain.cmake, tools/compare/speexdsp.cmake).
# See docs/COMPARISON.md for the methodology and the measured numbers.
include(FetchContent)
if(NOT TARGET benchmark::benchmark_main)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        # Commit pin, not the movable tag: tags can be re-pointed upstream.
        GIT_TAG c58e6d0710581e3a08d65c349664128a8d9a2461) # v1.9.1
    FetchContent_MakeAvailable(googlebenchmark)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
pkg_check_modules(SOXR REQUIRED IMPORTED_TARGET soxr)
include(${TAP_SR_COMPARE_TOOLS}/r8brain.cmake)
include(${TAP_SR_COMPARE_TOOLS}/speexdsp.cmake)

add_executable(tap_sr_bridge_bench_compare bench_compare.cpp)
target_link_libraries(tap_sr_bridge_bench_compare PRIVATE
    tap::sr::bridge
    tap_sr_bridge_warnings
    benchmark::benchmark_main
    PkgConfig::SAMPLERATE
    PkgConfig::SOXR
    tap_sr_cmp_r8brain
    tap_sr_cmp_speex_fixed
    tap_sr_cmp_speex_float)
