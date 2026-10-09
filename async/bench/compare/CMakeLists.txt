# Head-to-head computational comparison against general-purpose resamplers
# (libsamplerate, soxr, r8brain-free-src, SpeexDSP) at a fixed, known
# near-unity ratio. Host-only: libsamplerate and soxr come from the system
# (pkg-config); r8brain (header-only) and SpeexDSP's resample.c (built twice,
# fixed and float) are fetched at commit pins (cmake/r8brain.cmake,
# cmake/speexdsp.cmake).
# See docs/COMPARISON.md for the methodology and the measured numbers.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
pkg_check_modules(SOXR REQUIRED IMPORTED_TARGET soxr)
include(${PROJECT_SOURCE_DIR}/cmake/r8brain.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/speexdsp.cmake)

add_executable(tap_sr_async_bench_compare bench_compare.cpp)
target_link_libraries(tap_sr_async_bench_compare PRIVATE
    tap::sr::async
    tap_sr_async_warnings
    benchmark::benchmark_main
    PkgConfig::SAMPLERATE
    PkgConfig::SOXR
    tap_sr_async_r8brain
    tap_sr_async_speex_fixed
    tap_sr_async_speex_float)
