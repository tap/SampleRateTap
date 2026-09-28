cmake_minimum_required(VERSION 3.24)
project(SampleRateTap VERSION 0.1.0 LANGUAGES CXX)

# Shared Tap-family substrate (tap::dsp): kaiser design math, sample-format
# traits, FIR dot kernels, row-sum quantization, measurement instruments.
# Pinned once at the repository root (submodules/dsptap); added here only
# when this engine is configured on its own (cmake -S async).
if(NOT TARGET tap::dsp)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../submodules/dsptap
                     ${CMAKE_CURRENT_BINARY_DIR}/submodules/dsptap)
endif()

add_library(SampleRateTap INTERFACE)
add_library(SampleRateTap::SampleRateTap ALIAS SampleRateTap)
# The family-wide alias, per the namespace convention in taphouse's README: one
# `tap::<library>` sub-namespace per repo, and the CMake alias matches it. The
# older spelling above stays so existing consumers keep working; prefer
# `tap::samplerate` in new code.
add_library(tap::samplerate ALIAS SampleRateTap)
target_include_directories(SampleRateTap INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(SampleRateTap INTERFACE cxx_std_20)
target_link_libraries(SampleRateTap INTERFACE tap::dsp)

if(PROJECT_IS_TOP_LEVEL)
    option(SRT_BUILD_TESTS "Build SampleRateTap tests" ON)
    option(SRT_BUILD_EXAMPLES "Build SampleRateTap examples" ON)
else()
    option(SRT_BUILD_TESTS "Build SampleRateTap tests" OFF)
    option(SRT_BUILD_EXAMPLES "Build SampleRateTap examples" OFF)
endif()

# Warning flags for this project's own tests/examples; not propagated to consumers.
option(SRT_WERROR "Treat warnings as errors in SampleRateTap's own targets" OFF)
add_library(srt_warnings INTERFACE)
target_compile_options(srt_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->)
if(SRT_WERROR)
    target_compile_options(srt_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()

if(SRT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SRT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Host-only (Google Benchmark needs threads); see docs/PERFORMANCE.md.
option(SRT_BUILD_BENCHMARKS "Build SampleRateTap benchmarks" OFF)
if(SRT_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

# Head-to-head vs libsamplerate/soxr at a fixed known ratio (host-only;
# needs the system packages). See docs/COMPARISON.md.
option(SRT_BUILD_COMPARE_BENCH "Build resampler comparison benchmarks" OFF)
if(SRT_BUILD_COMPARE_BENCH)
    if(NOT SRT_BUILD_BENCHMARKS)
        message(FATAL_ERROR "SRT_BUILD_COMPARE_BENCH requires SRT_BUILD_BENCHMARKS")
    endif()
    add_subdirectory(bench/compare)
endif()

# Fixed deterministic workloads for the QEMU instruction-count ratchet;
# buildable for any target including bare metal. See docs/PERFORMANCE.md.
option(SRT_BUILD_ICOUNT_BENCH "Build instruction-count ratchet workloads" OFF)
if(SRT_BUILD_ICOUNT_BENCH)
    add_subdirectory(bench/icount)
endif()

# C ABI shared library for FFI consumers (see notebooks/asrc_demo.ipynb).
option(SRT_BUILD_CAPI "Build the C ABI shared library" OFF)
if(SRT_BUILD_CAPI)
    add_subdirectory(capi)
endif()

# ctypes shim over r8brain-free-src for the comparison notebook (fetched at a
# commit pin; comparison-only, never linked into the library). See
# notebooks/asrc_comparison.ipynb and docs/COMPARISON.md.
option(SRT_BUILD_COMPARE_SHIM "Build the r8brain shim for the comparison notebook" OFF)
if(SRT_BUILD_COMPARE_SHIM)
    add_subdirectory(tools/compare_shim)
endif()
