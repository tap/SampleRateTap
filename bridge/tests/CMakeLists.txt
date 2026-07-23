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

add_executable(tap_ratio_tests
    test_design.cpp
    test_phase_table.cpp
    test_schedule.cpp
    test_skeleton.cpp)
target_link_libraries(tap_ratio_tests PRIVATE
    tap::ratio
    tap_ratio_warnings)

target_link_libraries(tap_ratio_tests PRIVATE GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(tap_ratio_tests DISCOVERY_TIMEOUT 120 PROPERTIES TIMEOUT 900)
