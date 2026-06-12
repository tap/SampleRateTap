include(FetchContent)

# Threads are optional: bare-metal targets (e.g. Cortex-M) have no
# std::thread, so the two-thread stress test is only compiled where they
# exist and GoogleTest is built single-threaded otherwise. The probe is
# skipped entirely on bare metal: newlib ships pthread.h/regex.h stub
# headers that make POSIX feature detection succeed spuriously.
if(NOT SRT_BARE_METAL)
    find_package(Threads QUIET)
endif()
if(NOT Threads_FOUND)
    set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
endif()
if(SRT_BARE_METAL)
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
FetchContent_MakeAvailable(googletest)

add_executable(srt_tests
    test_kaiser.cpp
    test_spsc_ring.cpp
    test_polyphase.cpp
    test_fixed_point.cpp
    test_servo.cpp
    test_asrc_lock.cpp
    test_asrc_quality.cpp
    test_asrc_quality_16k.cpp
    test_fade.cpp
    test_hardening.cpp
    test_latency.cpp
    test_multichannel.cpp)
target_link_libraries(srt_tests PRIVATE
    SampleRateTap::SampleRateTap
    srt_warnings)
target_include_directories(srt_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(Threads_FOUND)
    target_sources(srt_tests PRIVATE test_spsc_ring_threads.cpp)
    target_link_libraries(srt_tests PRIVATE Threads::Threads)
endif()

if(SRT_BARE_METAL)
    # One-shot mode (e.g. Cortex-M55 under qemu-system-arm): a custom main
    # bakes in the emulation-sized filter (no argv on the target), and the
    # run is judged on gtest's summary text because semihosting does not
    # reliably propagate exit codes through the emulator.
    target_sources(srt_tests PRIVATE bare_metal_main.cpp)
    target_link_libraries(srt_tests PRIVATE GTest::gtest)
    add_test(NAME srt_tests_emulated COMMAND srt_tests)
    set_tests_properties(srt_tests_emulated PROPERTIES
        PASS_REGULAR_EXPRESSION "SRT_TESTS_COMPLETE rc=0"
        FAIL_REGULAR_EXPRESSION "\\[  FAILED  \\]"
        TIMEOUT 1800)
else()
    target_link_libraries(srt_tests PRIVATE GTest::gtest_main)
    # Generous timeouts: discovery and the DSP-measurement tests run orders
    # of magnitude slower under instruction-set emulation (e.g. qemu-hexagon).
    include(GoogleTest)
    gtest_discover_tests(srt_tests DISCOVERY_TIMEOUT 120 PROPERTIES TIMEOUT 900)
endif()
