# DRAFT (audit round 2): root CMakeLists.txt at step 1c.
cmake_minimum_required(VERSION 3.24)
project(SampleRateTap LANGUAGES CXX)   # languages must equal both engines' (G7)
enable_testing()

# DEFAULT, never FORCE: CI passes *_BUILD_EXAMPLES=OFF on every bare-metal /
# Hexagon job, and async/examples does find_package(Threads REQUIRED).
# option() here creates the cache entry first, so the engines' own
# PROJECT_IS_TOP_LEVEL-dependent option() calls become no-ops, while -D wins.
option(SRT_BUILD_TESTS          "async tests"    ON)
option(SRT_BUILD_EXAMPLES       "async examples" ON)
option(TAP_RATIO_BUILD_TESTS    "ratio tests"    ON)
option(TAP_RATIO_BUILD_EXAMPLES "ratio examples" ON)

# Stale-option tripwire (extends D7 to CMake options, needed from step 3.5):
# an unknown -D is only a "Manually-specified variables were not used"
# warning, so a CI line still passing -DSRT_WERROR=ON after the rename would
# silently DROP the warnings gate. Enable this block in the step-3.5 commit.
# foreach(_old SRT_WERROR SRT_BUILD_TESTS SRT_BUILD_EXAMPLES SRT_BUILD_CAPI
#              SRT_BUILD_ICOUNT_BENCH SRT_BUILD_BENCHMARKS TAP_RATIO_WERROR
#              TAP_RATIO_BUILD_TESTS TAP_RATIO_BUILD_EXAMPLES TAP_RATIO_BUILD_CAPI
#              TAP_RATIO_BUILD_ICOUNT_BENCH)
#   if(DEFINED ${_old} OR DEFINED CACHE{${_old}})
#     message(FATAL_ERROR "${_old} was renamed (MONOREPO_PLAN step 3.5)")
#   endif()
# endforeach()

add_subdirectory(submodules/dsptap)
add_subdirectory(async)
add_subdirectory(ratio)

# gtest is made available ONCE, by whichever engine's tests/ runs first
# (async). ratio/tests' set(INSTALL_GTEST OFF ...) and its Threads probe are
# then no-ops: hoist both gtest settings here so the result does not depend
# on add_subdirectory order (and so 4.2's install test does not install gtest).
