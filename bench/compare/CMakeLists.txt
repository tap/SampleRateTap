# Head-to-head computational comparison against general-purpose resamplers
# (libsamplerate, soxr) at a fixed, known near-unity ratio. Host-only:
# the competitor libraries come from the system (pkg-config). See
# docs/COMPARISON.md for the methodology and the measured numbers.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
pkg_check_modules(SOXR REQUIRED IMPORTED_TARGET soxr)

add_executable(srt_bench_compare bench_compare.cpp)
target_link_libraries(srt_bench_compare PRIVATE
    SampleRateTap::SampleRateTap
    srt_warnings
    benchmark::benchmark_main
    PkgConfig::SAMPLERATE
    PkgConfig::SOXR)
