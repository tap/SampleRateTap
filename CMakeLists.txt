cmake_minimum_required(VERSION 3.24)
project(SampleRateTap VERSION 0.1.0 LANGUAGES CXX)

add_library(SampleRateTap INTERFACE)
add_library(SampleRateTap::SampleRateTap ALIAS SampleRateTap)
target_include_directories(SampleRateTap INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(SampleRateTap INTERFACE cxx_std_20)

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
