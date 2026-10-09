cmake_minimum_required(VERSION 3.24)
project(tap_sr_bridge LANGUAGES CXX)
# The family version is TAP_SR_VERSION_* in include/tap/sr/bridge/ratio.h
# and the root project() (D13); the engine subproject carries none.
if(PROJECT_IS_TOP_LEVEL)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/retired_options.cmake)
endif()

# ==============================================================================
# bridge — synchronous 44.1 <-> 48 kHz sample rate conversion, as fast as
# possible. Header-only C++20 (include/tap/sr/bridge/), built on the Tap family's
# shared FIR substrate from DspTap (kaiser design, sample-format traits, dot
# kernels, quantization, measurement instruments). See PLAN.md for the charter
# and milestones; this is the M1 skeleton.
# ==============================================================================

# Shared Tap-family substrate (tap::dsp), pinned once at the repository root
# (submodules/dsptap); added here only when this engine is configured on its
# own (cmake -S bridge).
if(NOT TARGET tap::dsp)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../submodules/dsptap
                     ${CMAKE_CURRENT_BINARY_DIR}/submodules/dsptap)
endif()

add_library(tap_sr_bridge INTERFACE)
add_library(tap::sr::bridge ALIAS tap_sr_bridge)
target_include_directories(tap_sr_bridge INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(tap_sr_bridge INTERFACE cxx_std_20)
target_link_libraries(tap_sr_bridge INTERFACE tap::dsp)

if(PROJECT_IS_TOP_LEVEL)
    option(TAP_SR_BUILD_TESTS "Build the bridge engine's tests" ON)
    option(TAP_SR_BUILD_EXAMPLES "Build the bridge engine's examples" ON)
else()
    option(TAP_SR_BUILD_TESTS "Build the bridge engine's tests" OFF)
    option(TAP_SR_BUILD_EXAMPLES "Build the bridge engine's examples" OFF)
endif()

# C ABI shared library for FFI consumers (the notebooks drive the shipping
# C++ through it — see notebooks/ratio_demo.ipynb).
option(TAP_SR_BUILD_CAPI "Build the C ABI shared library" OFF)

# The family's comparison tooling (competitor fetch recipes, the shims the
# comparison notebook loads, the bare-metal mutex stub): tools/compare at the
# repository root, shared with the sibling engines' comparisons.
set(TAP_SR_COMPARE_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/compare")
option(TAP_SR_BUILD_COMPARE_SHIM "Build the comparison notebooks' competitor shims" OFF)
if(TAP_SR_BUILD_COMPARE_SHIM AND NOT TARGET tap_sr_cmp_r8b_shim)
    add_subdirectory(${TAP_SR_COMPARE_TOOLS}/shim ${CMAKE_CURRENT_BINARY_DIR}/tools/compare/shim)
endif()

# Fixed deterministic workloads for the QEMU instruction-count ratchet;
# buildable for any target including bare metal. See PLAN.md section 7.
option(TAP_SR_BUILD_ICOUNT_BENCH "Build instruction-count ratchet workloads" OFF)

# The sibling async engine, dev-only (never part of the shipped tap::sr::bridge
# target): the golden cross-validation test and the bluetooth_bridge example
# compose against its near-unity ASRC. Consumed headers-only via include path,
# so this engine's code never links async's target (PLAN.md 4.2);
# tap::dsp serves its "tap/dsp/..." includes.
if(TAP_SR_BUILD_TESTS OR TAP_SR_BUILD_EXAMPLES)
    add_library(tap_sr_async_headers INTERFACE)
    # SYSTEM: dependency headers are exempt from this repo's warning gates
    # (MSVC /W4 otherwise fires benign C4324 on the ASRC ring's deliberate
    # cache-line alignment padding, and -Werror turns it fatal).
    cmake_path(SET _tap_async_include NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/../async/include")
    target_include_directories(tap_sr_async_headers SYSTEM INTERFACE ${_tap_async_include})
endif()

# Warning flags for this project's own tests; never exported to consumers of
# the INTERFACE library.
option(TAP_SR_BRIDGE_WERROR "Treat warnings as errors in the bridge engine's own targets" OFF)
add_library(tap_sr_bridge_warnings INTERFACE)
target_compile_options(tap_sr_bridge_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->)
if(TAP_SR_BRIDGE_WERROR)
    target_compile_options(tap_sr_bridge_warnings INTERFACE
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

if(TAP_SR_BUILD_CAPI)
    add_subdirectory(capi)
endif()

if(TAP_SR_BUILD_ICOUNT_BENCH)
    add_subdirectory(bench/icount)
endif()

# Head-to-head vs libsamplerate/soxr/r8brain/SpeexDSP at the 44.1 <-> 48 pair
# (host-only; needs the system packages). See docs/COMPARISON.md.
option(TAP_SR_BUILD_COMPARE_BENCH "Build resampler comparison benchmarks" OFF)
if(TAP_SR_BUILD_COMPARE_BENCH)
    add_subdirectory(bench/compare)
endif()
