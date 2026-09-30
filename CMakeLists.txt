cmake_minimum_required(VERSION 3.24)
# The tap::sr sample-rate family: one tree, two engines. async/ is the
# near-unity asynchronous converter, bridge/ the synchronous 44.1 <-> 48 kHz
# converter. Each engine is a subproject with its own target; this file
# composes them over one tap::dsp, declares the family options (D9) and the
# umbrella target, and adds the family's own tests (the dependency rule of
# PLAN.md 4.2).
project(SampleRateTap VERSION 0.4.0 LANGUAGES CXX)
enable_testing()

include(cmake/retired_options.cmake)

# Family options (D9). Every configure builds both engines; CI selects an
# engine only when running tests, by ctest label. Defaults, never FORCE:
# every bare-metal and Hexagon job passes TAP_SR_BUILD_EXAMPLES=OFF (async's
# examples need Threads), and a -D on the command line must win. Declared
# before the engines, so their own PROJECT_IS_TOP_LEVEL-dependent option()
# calls find the cache entry set.
option(TAP_SR_BUILD_TESTS "Build both engines' tests and the family's own" ON)
option(TAP_SR_BUILD_EXAMPLES "Build both engines' examples" ON)
option(TAP_SR_BUILD_CAPI "Build both engines' C ABI shared libraries" OFF)
option(TAP_SR_BUILD_ICOUNT_BENCH "Build both engines' instruction-count ratchet workloads" OFF)
option(TAP_SR_BUILD_BENCHMARKS "Build the async engine's benchmarks (host-only)" OFF)
option(TAP_SR_BUILD_COMPARE_BENCH "Build the async engine's resampler comparison benchmarks (host-only)" OFF)
option(TAP_SR_BUILD_COMPARE_SHIM "Build the async engine's r8brain shim for its comparison notebook" OFF)
# Warnings as errors, per engine: each keeps its own warning policy.
option(TAP_SR_ASYNC_WERROR "Treat warnings as errors in the async engine's own targets" OFF)
option(TAP_SR_BRIDGE_WERROR "Treat warnings as errors in the bridge engine's own targets" OFF)

# GoogleTest is fetched once, by whichever engine's tests/ is configured
# first; the other engine's FetchContent_MakeAvailable is then a no-op. Its
# settings are hoisted here so the result does not depend on that order.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# The Threads probe is skipped on bare metal: newlib ships pthread.h stubs
# that make POSIX feature detection succeed spuriously.
if(NOT TAP_SR_BARE_METAL)
    find_package(Threads QUIET)
endif()
if(NOT Threads_FOUND)
    set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
endif()

# The shared substrate (tap::dsp), once for both engines; each engine's
# guard skips its own copy when this target exists.
add_subdirectory(submodules/dsptap)
add_subdirectory(async)
add_subdirectory(bridge)

# The umbrella: both engines over one substrate. Engines never link each
# other (4.2); a consumer that wants both links this.
add_library(tap_sr INTERFACE)
target_link_libraries(tap_sr INTERFACE tap::sr::async tap::sr::bridge)
add_library(tap::sr ALIAS tap_sr)

if(TAP_SR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
