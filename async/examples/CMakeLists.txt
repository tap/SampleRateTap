find_package(Threads REQUIRED)

add_executable(drifting_clocks drifting_clocks.cpp)
target_link_libraries(drifting_clocks PRIVATE
    SampleRateTap::SampleRateTap
    srt_warnings
    Threads::Threads)

# Real-hardware duplex bridge (docs/HARDWARE_TESTING.md Setup 1); Linux-only.
find_package(ALSA QUIET)
if(ALSA_FOUND)
    add_executable(srt_alsa_bridge alsa_bridge.cpp)
    target_link_libraries(srt_alsa_bridge PRIVATE
        SampleRateTap::SampleRateTap
        srt_warnings
        ALSA::ALSA
        Threads::Threads)
else()
    message(STATUS "ALSA not found; skipping srt_alsa_bridge example")
endif()
