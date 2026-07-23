cmake_minimum_required(VERSION 3.24)
project(RatioTap VERSION 0.1.0 LANGUAGES CXX)

# ==============================================================================
# RatioTap — synchronous 44.1 <-> 48 kHz sample rate conversion, as fast as
# possible. Header-only C++20 (include/tap/ratio/), built on the Tap family's
# shared FIR substrate from DspTap (kaiser design, sample-format traits, dot
# kernels, quantization, measurement instruments). See PLAN.md for the charter
# and milestones; this is the M1 skeleton.
# ==============================================================================

# Shared Tap-family substrate (tap::dsp), pinned as a submodule per the DspTap
# release flow (changes land there first, consumers bump the pin). Declares
# its own C language support for the vendored Ooura sources.
add_subdirectory(submodules/dsptap)

add_library(tap_ratio INTERFACE)
add_library(tap::ratio ALIAS tap_ratio)
target_include_directories(tap_ratio INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(tap_ratio INTERFACE cxx_std_20)
target_link_libraries(tap_ratio INTERFACE tap::dsp)

if(PROJECT_IS_TOP_LEVEL)
    option(TAP_RATIO_BUILD_TESTS "Build RatioTap tests" ON)
    option(TAP_RATIO_BUILD_EXAMPLES "Build RatioTap examples" ON)
else()
    option(TAP_RATIO_BUILD_TESTS "Build RatioTap tests" OFF)
    option(TAP_RATIO_BUILD_EXAMPLES "Build RatioTap examples" OFF)
endif()

# C ABI shared library for FFI consumers (the notebooks drive the shipping
# C++ through it — see notebooks/ratio_demo.ipynb).
option(TAP_RATIO_BUILD_CAPI "Build the C ABI shared library" OFF)

# SampleRateTap, dev-only (never part of the shipped tap::ratio target): the
# golden cross-validation test and the bluetooth_bridge example compose
# against its near-unity ASRC. Consumed headers-only via include path — its
# own CMake would add_subdirectory a second dsptap and collide with ours;
# both repos pin the identical dsptap tree, so our tap::dsp serves its
# "tap/dsp/..." includes.
if(TAP_RATIO_BUILD_TESTS OR TAP_RATIO_BUILD_EXAMPLES)
    add_library(srt_headers INTERFACE)
    target_include_directories(srt_headers INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/submodules/sampleratetap/include)
endif()

# Warning flags for this project's own tests; never exported to consumers of
# the INTERFACE library.
option(TAP_RATIO_WERROR "Treat warnings as errors in RatioTap's own targets" OFF)
add_library(tap_ratio_warnings INTERFACE)
target_compile_options(tap_ratio_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->)
if(TAP_RATIO_WERROR)
    target_compile_options(tap_ratio_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()

if(TAP_RATIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TAP_RATIO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(TAP_RATIO_BUILD_CAPI)
    add_subdirectory(tools/capi)
endif()
