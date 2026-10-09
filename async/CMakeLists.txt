cmake_minimum_required(VERSION 3.24)
project(tap_sr_async LANGUAGES CXX)
# The family version is TAP_SR_VERSION_* in include/tap/sr/async/async.h and
# the root project() (D13); the engine subproject carries none.
if(PROJECT_IS_TOP_LEVEL)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/retired_options.cmake)
endif()

# Shared Tap-family substrate (tap::dsp): kaiser design math, sample-format
# traits, FIR dot kernels, row-sum quantization, measurement instruments.
# Pinned once at the repository root (submodules/dsptap); added here only
# when this engine is configured on its own (cmake -S async).
if(NOT TARGET tap::dsp)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../submodules/dsptap
                     ${CMAKE_CURRENT_BINARY_DIR}/submodules/dsptap)
endif()

add_library(tap_sr_async INTERFACE)
add_library(tap::sr::async ALIAS tap_sr_async)
target_include_directories(tap_sr_async INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(tap_sr_async INTERFACE cxx_std_20)
target_link_libraries(tap_sr_async INTERFACE tap::dsp)

if(PROJECT_IS_TOP_LEVEL)
    option(TAP_SR_BUILD_TESTS "Build SampleRateTap tests" ON)
    option(TAP_SR_BUILD_EXAMPLES "Build SampleRateTap examples" ON)
else()
    option(TAP_SR_BUILD_TESTS "Build SampleRateTap tests" OFF)
    option(TAP_SR_BUILD_EXAMPLES "Build SampleRateTap examples" OFF)
endif()

# Warning flags for this project's own tests/examples; not propagated to consumers.
option(TAP_SR_ASYNC_WERROR "Treat warnings as errors in SampleRateTap's own targets" OFF)
add_library(tap_sr_async_warnings INTERFACE)
target_compile_options(tap_sr_async_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->)
if(TAP_SR_ASYNC_WERROR)
    target_compile_options(tap_sr_async_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()

if(TAP_SR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TAP_SR_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Host-only (Google Benchmark needs threads); see docs/PERFORMANCE.md.
option(TAP_SR_BUILD_BENCHMARKS "Build SampleRateTap benchmarks" OFF)
if(TAP_SR_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

# The family's comparison tooling (competitor fetch recipes, the shims, the
# bare-metal mutex stub), shared with the sibling engines' comparisons.
set(TAP_SR_COMPARE_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/compare")

# Head-to-head vs libsamplerate/soxr/r8brain/SpeexDSP at a fixed known ratio
# (host-only; needs the system packages). See docs/COMPARISON.md.
option(TAP_SR_BUILD_COMPARE_BENCH "Build resampler comparison benchmarks" OFF)
if(TAP_SR_BUILD_COMPARE_BENCH)
    if(NOT TAP_SR_BUILD_BENCHMARKS)
        message(FATAL_ERROR "TAP_SR_BUILD_COMPARE_BENCH requires TAP_SR_BUILD_BENCHMARKS")
    endif()
    add_subdirectory(bench/compare)
endif()

# Fixed deterministic workloads for the QEMU instruction-count ratchet;
# buildable for any target including bare metal. See docs/PERFORMANCE.md.
option(TAP_SR_BUILD_ICOUNT_BENCH "Build instruction-count ratchet workloads" OFF)
if(TAP_SR_BUILD_ICOUNT_BENCH)
    add_subdirectory(bench/icount)
endif()

# C ABI shared library for FFI consumers (see notebooks/asrc_demo.ipynb).
option(TAP_SR_BUILD_CAPI "Build the C ABI shared library" OFF)
if(TAP_SR_BUILD_CAPI)
    add_subdirectory(capi)
endif()

# The family's ctypes shims over r8brain-free-src, SpeexDSP and soxr for the
# comparison notebooks (tools/compare/shim; the competitors are fetched at
# commit pins, comparison-only, never linked into the library). Added here
# only when this engine is configured on its own; the root adds them once.
# See notebooks/asrc_comparison.ipynb and docs/COMPARISON.md.
option(TAP_SR_BUILD_COMPARE_SHIM "Build the comparison notebooks' competitor shims" OFF)
if(TAP_SR_BUILD_COMPARE_SHIM AND NOT TARGET tap_sr_cmp_r8b_shim)
    add_subdirectory(${TAP_SR_COMPARE_TOOLS}/shim ${CMAKE_CURRENT_BINARY_DIR}/tools/compare/shim)
endif()
