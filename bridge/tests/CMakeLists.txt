include(FetchContent)

find_package(Threads QUIET)
if(NOT Threads_FOUND)
    set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    # Commit pin, not the movable tag: tags can be re-pointed upstream.
    GIT_TAG f8d7d77c06936315286eb55f8de22cd23c188571 # v1.14.0
    FIND_PACKAGE_ARGS NAMES GTest)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# SampleRateTap, test-only (PLAN section 6.3): the golden cross-validation
# drives its fractional_resampler at pinned eps against this library's exact
# engine. Header-only consumption via include path — its own CMake is NOT
# added (it would add_subdirectory its dsptap submodule and collide with
# ours; both repos pin the identical dsptap tree, so our tap::dsp serves its
# "tap/dsp/..." includes). Never linked into the shipped target.
add_library(srt_headers INTERFACE)
target_include_directories(srt_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../submodules/sampleratetap/include)

add_executable(tap_ratio_tests
    test_converter.cpp
    test_converter_fixed_point.cpp
    test_cross_validation.cpp
    test_design.cpp
    test_phase_table.cpp
    test_schedule.cpp
    test_skeleton.cpp)
# The tests' own headers (the committed reference vectors) resolve relative
# to this directory.
target_include_directories(tap_ratio_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tap_ratio_tests PRIVATE
    tap::ratio
    srt_headers
    tap_ratio_warnings)

target_link_libraries(tap_ratio_tests PRIVATE GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(tap_ratio_tests DISCOVERY_TIMEOUT 120 PROPERTIES TIMEOUT 900)
