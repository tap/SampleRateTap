include(FetchContent)

# Threads are optional: bare-metal targets (Cortex-M33/M55) have no
# std::thread, so GoogleTest is built single-threaded there. The probe is
# skipped entirely on bare metal: newlib ships pthread.h/regex.h stub
# headers that make POSIX feature detection succeed spuriously.
if(NOT TAP_RATIO_BARE_METAL)
    find_package(Threads QUIET)
endif()
if(NOT Threads_FOUND)
    set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
endif()
if(TAP_RATIO_BARE_METAL)
    # Value-checked feature macros only: GTEST_HAS_DEATH_TEST is tested with
    # #ifdef in gtest 1.14, so it must stay undefined (no GTEST_OS_* is
    # detected on bare metal, which disables it).
    add_compile_definitions(GTEST_HAS_POSIX_RE=0 GTEST_HAS_STREAM_REDIRECTION=0
                            GTEST_HAS_FILE_SYSTEM=0)
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
if(Threads_FOUND)
    target_link_libraries(tap_ratio_tests PRIVATE Threads::Threads)
endif()

if(TAP_RATIO_BARE_METAL)
    # One-shot mode (Cortex-M33/M55 under qemu-system-arm): a custom main
    # bakes in the emulation-sized filter (no argv on the target), and the
    # run is judged on gtest's summary text because semihosting does not
    # reliably propagate exit codes through the emulator.
    target_sources(tap_ratio_tests PRIVATE bare_metal_main.cpp)
    target_link_libraries(tap_ratio_tests PRIVATE GTest::gtest)
    add_test(NAME tap_ratio_tests_emulated COMMAND tap_ratio_tests)
    set_tests_properties(tap_ratio_tests_emulated PROPERTIES
        PASS_REGULAR_EXPRESSION "TAP_RATIO_TESTS_COMPLETE rc=0"
        FAIL_REGULAR_EXPRESSION "\\[  FAILED  \\]"
        TIMEOUT 1800)
else()
    target_link_libraries(tap_ratio_tests PRIVATE GTest::gtest_main)
    # Generous timeouts: discovery and the DSP-measurement tests run orders
    # of magnitude slower under instruction-set emulation (e.g. qemu-hexagon).
    include(GoogleTest)
    gtest_discover_tests(tap_ratio_tests DISCOVERY_TIMEOUT 120 PROPERTIES TIMEOUT 900)
endif()
