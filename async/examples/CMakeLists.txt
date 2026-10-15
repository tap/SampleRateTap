find_package(Threads REQUIRED)

add_executable(drifting_clocks drifting_clocks.cpp)
target_link_libraries(drifting_clocks PRIVATE
    tap::sr::async
    tap_sr_async_warnings
    Threads::Threads)

# Real-hardware duplex bridge (docs/HARDWARE_TESTING.md Setup 1); Linux-only.
find_package(ALSA QUIET)
if(ALSA_FOUND)
    add_executable(tap_sr_async_alsa_bridge alsa_bridge.cpp)
    target_link_libraries(tap_sr_async_alsa_bridge PRIVATE
        tap::sr::async
        tap_sr_async_warnings
        ALSA::ALSA
        Threads::Threads)
    # No hardware on CI: the measurement's guard is what can be checked there.
    # A plugin device must be refused before anything is opened.
    add_test(NAME async.Examples.AlsaBridgeRefusesPluginDevices
             COMMAND tap_sr_async_alsa_bridge --in default --out hw:0)
    set_tests_properties(async.Examples.AlsaBridgeRefusesPluginDevices PROPERTIES
        PASS_REGULAR_EXPRESSION "refusing 'default': only hw: devices"
        LABELS async)
else()
    message(STATUS "ALSA not found; skipping tap_sr_async_alsa_bridge example")
endif()

# The bench's analysis (alsa_bridge_report.py): its arithmetic against a
# synthetic run with known rates, wherever Python is.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME async.Examples.AlsaBridgeReportSelfTest
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/alsa_bridge_report.py --self-test)
    set_tests_properties(async.Examples.AlsaBridgeReportSelfTest PROPERTIES LABELS async)
endif()
