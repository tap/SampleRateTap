cmake_minimum_required(VERSION 3.24)
project(tap_sr_rational LANGUAGES CXX)
# The family version is TAP_SR_VERSION_* in include/tap/sr/rational/rational.h
# and the root project() (D13); the engine subproject carries none.
if(PROJECT_IS_TOP_LEVEL)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/retired_options.cmake)
endif()

# ==============================================================================
# rational — synchronous small-factor L/M conversion within a rate family
# (L, M in {2^a * 3^b}), as fast as possible. Header-only C++20
# (include/tap/sr/rational/), built on the Tap family's shared substrate from
# DspTap (the L-th-band designer, the stage composition, sample-format traits,
# dot kernels, quantization). See PLAN.md for the charter and milestones.
# ==============================================================================

# Shared Tap-family substrate (tap::dsp), pinned once at the repository root
# (submodules/dsptap); added here only when this engine is configured on its
# own (cmake -S rational).
if(NOT TARGET tap::dsp)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../submodules/dsptap
                     ${CMAKE_CURRENT_BINARY_DIR}/submodules/dsptap)
endif()

add_library(tap_sr_rational INTERFACE)
add_library(tap::sr::rational ALIAS tap_sr_rational)
target_include_directories(tap_sr_rational INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(tap_sr_rational INTERFACE cxx_std_20)
target_link_libraries(tap_sr_rational INTERFACE tap::dsp)

if(PROJECT_IS_TOP_LEVEL)
    option(TAP_SR_BUILD_TESTS "Build the rational engine's tests" ON)
    option(TAP_SR_BUILD_EXAMPLES "Build the rational engine's examples" ON)
else()
    option(TAP_SR_BUILD_TESTS "Build the rational engine's tests" OFF)
    option(TAP_SR_BUILD_EXAMPLES "Build the rational engine's examples" OFF)
endif()

# C ABI shared library for FFI consumers (the notebooks drive the shipping
# C++ through it — notebooks/matrix.ipynb).
option(TAP_SR_BUILD_CAPI "Build the C ABI shared library" OFF)

# Fixed deterministic workloads for the QEMU instruction-count ratchet;
# buildable for any target including bare metal (PLAN.md section 5).
option(TAP_SR_BUILD_ICOUNT_BENCH "Build instruction-count ratchet workloads" OFF)

# Warning flags for this engine's own tests; never exported to consumers of
# the INTERFACE library.
option(TAP_SR_RATIONAL_WERROR "Treat warnings as errors in the rational engine's own targets" OFF)
add_library(tap_sr_rational_warnings INTERFACE)
target_compile_options(tap_sr_rational_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->)
if(TAP_SR_RATIONAL_WERROR)
    target_compile_options(tap_sr_rational_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()

# The sibling bridge engine, dev-only (never part of the shipped
# tap::sr::rational target): the coverage matrix's cross-family rows compose
# bridge's converter with this engine's stages (PLAN.md 3.5 / 3.6), through a
# test-side adapter. Consumed headers-only via include path, so this engine's
# code never links bridge's target (PLAN.md 4.2); tap::dsp serves its
# "tap/dsp/..." includes.
if(TAP_SR_BUILD_TESTS)
    add_library(tap_sr_bridge_headers INTERFACE)
    # SYSTEM: dependency headers are exempt from this engine's warning gates.
    cmake_path(SET _tap_bridge_include NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/../bridge/include")
    target_include_directories(tap_sr_bridge_headers SYSTEM INTERFACE ${_tap_bridge_include})
endif()

if(TAP_SR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TAP_SR_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(TAP_SR_BUILD_CAPI)
    add_subdirectory(capi)
endif()

if(TAP_SR_BUILD_ICOUNT_BENCH)
    add_subdirectory(bench/icount)
endif()
