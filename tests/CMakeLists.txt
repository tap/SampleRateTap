include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    FIND_PACKAGE_ARGS NAMES GTest)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

find_package(Threads REQUIRED)

add_executable(srt_tests
    test_kaiser.cpp
    test_spsc_ring.cpp
    test_polyphase.cpp
    test_servo.cpp
    test_asrc_lock.cpp
    test_asrc_quality.cpp
    test_latency.cpp)
target_link_libraries(srt_tests PRIVATE
    SampleRateTap::SampleRateTap
    srt_warnings
    GTest::gtest_main
    Threads::Threads)
target_include_directories(srt_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(srt_tests PROPERTIES TIMEOUT 300)
