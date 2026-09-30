cmake_minimum_required(VERSION 3.24)
# The tap::sr sample-rate family: one tree, two engines. async/ is the
# near-unity asynchronous converter (was SampleRateTap), bridge/ the
# synchronous 44.1 <-> 48 kHz converter (was RatioTap). Each engine keeps its
# own project() and options until the monorepo migration's renames (step 3,
# docs/MONOREPO_PLAN.md); this file only composes them.
project(SampleRateTap LANGUAGES CXX)
enable_testing()

# Defaults, never FORCE: every bare-metal and Hexagon job passes
# *_BUILD_EXAMPLES=OFF (async's examples need Threads), and a -D on the
# command line must win. Declared before the engines, so their own
# PROJECT_IS_TOP_LEVEL-dependent option() calls find the cache entry set.
option(SRT_BUILD_TESTS "Build the async engine's tests" ON)
option(SRT_BUILD_EXAMPLES "Build the async engine's examples" ON)
option(TAP_RATIO_BUILD_TESTS "Build the bridge engine's tests" ON)
option(TAP_RATIO_BUILD_EXAMPLES "Build the bridge engine's examples" ON)

# GoogleTest is fetched once, by whichever engine's tests/ is configured
# first; the other engine's FetchContent_MakeAvailable is then a no-op. Its
# settings are hoisted here so the result does not depend on that order.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# The Threads probe is skipped on bare metal: newlib ships pthread.h stubs
# that make POSIX feature detection succeed spuriously.
if(NOT TAP_SR_BARE_METAL AND NOT TAP_SR_BARE_METAL)
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
