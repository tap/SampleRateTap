include(FetchContent)

# Threads are optional: bare-metal targets (e.g. Cortex-M) have no
# std::thread, so the two-thread stress test is only compiled where they
# exist and GoogleTest is built single-threaded otherwise.
find_package(Threads QUIET)
if(NOT Threads_FOUND)
    set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
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
    test_latency.cpp)
target_link_libraries(srt_tests PRIVATE
    SampleRateTap::SampleRateTap
    srt_warnings
    GTest::gtest_main)
target_include_directories(srt_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(Threads_FOUND)
    target_sources(srt_tests PRIVATE test_spsc_ring_threads.cpp)
    target_link_libraries(srt_tests PRIVATE Threads::Threads)
endif()

# Generous timeouts: discovery and the DSP-measurement tests run orders of
# magnitude slower under instruction-set emulation (e.g. qemu-hexagon).
include(GoogleTest)
gtest_discover_tests(srt_tests DISCOVERY_TIMEOUT 120 PROPERTIES TIMEOUT 900)
