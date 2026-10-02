include(FetchContent)

# Threads are optional: bare-metal targets (Cortex-M33/M55) have no
# std::thread, so GoogleTest is built single-threaded there. The probe is
# skipped entirely on bare metal: newlib ships pthread.h/regex.h stub
# headers that make POSIX feature detection succeed spuriously.
if(NOT TAP_SR_BARE_METAL)
    find_package(Threads QUIET)
endif()
if(NOT Threads_FOUND)
    set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
endif()
if(TAP_SR_BARE_METAL)
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

add_executable(tap_sr_rational_tests
    test_design.cpp
    test_output_hash.cpp
    test_ratio.cpp
    test_stage.cpp)
# The tests' own headers (the committed reference vectors) resolve relative
# to this directory.
target_include_directories(tap_sr_rational_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tap_sr_rational_tests PRIVATE
    tap::sr::rational
    tap_sr_rational_warnings)

if(Threads_FOUND)
    target_link_libraries(tap_sr_rational_tests PRIVATE Threads::Threads)
endif()

if(TAP_SR_BARE_METAL)
    # One-shot mode (Cortex-M33/M55 under qemu-system-arm): a custom main
    # bakes in the emulation-sized filter (no argv on the target), and the
    # run is judged on gtest's summary text because semihosting does not
    # reliably propagate exit codes through the emulator.
    target_sources(tap_sr_rational_tests PRIVATE bare_metal_main.cpp)
    target_link_libraries(tap_sr_rational_tests PRIVATE GTest::gtest)
    add_test(NAME tap_sr_rational_tests_emulated COMMAND tap_sr_rational_tests)
    set_tests_properties(tap_sr_rational_tests_emulated PROPERTIES
        PASS_REGULAR_EXPRESSION "TAP_SR_TESTS_COMPLETE rc=0"
        FAIL_REGULAR_EXPRESSION "\\[  FAILED  \\]"
        TIMEOUT 1800
        LABELS rational)
else()
    target_link_libraries(tap_sr_rational_tests PRIVATE GTest::gtest_main)
    include(GoogleTest)
    # The "rational." prefix and label (D16): ctest names stay unique across
    # the family's three suites, and every job selects an engine by label.
    gtest_discover_tests(tap_sr_rational_tests TEST_PREFIX "rational." DISCOVERY_TIMEOUT 120
                         PROPERTIES TIMEOUT 900 LABELS rational)
endif()

# The charter is a static_assert (R1): ratio<5, 1>, ratio<4, 2> and
# ratio<2, 2> must fail to compile WITH the charter's message. A compile that
# must fail cannot be a target of this build, so compile_fail/ is its own
# small project that try_compile()s each case and fails its configure when a
# case compiles or the message is missing; this test runs that configure
# with this build's compiler (or toolchain file, on the cross legs; the
# cases build no executable, so a cross compiler without a runtime still
# serves).
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    get_filename_component(_tc "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    set(_compiler_args "-DCMAKE_TOOLCHAIN_FILE=${_tc}")
else()
    set(_compiler_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
endif()
add_test(NAME rational.Ratio.ChartersFailToCompileWithTheMessage
         COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}"
                 -S ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail
                 -B ${CMAKE_CURRENT_BINARY_DIR}/compile_fail
                 ${_compiler_args}
                 -DTAP_SR_RATIONAL_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/../include)
set_tests_properties(rational.Ratio.ChartersFailToCompileWithTheMessage PROPERTIES LABELS rational TIMEOUT 300)
