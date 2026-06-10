find_package(Threads REQUIRED)

add_executable(drifting_clocks drifting_clocks.cpp)
target_link_libraries(drifting_clocks PRIVATE
    SampleRateTap::SampleRateTap
    srt_warnings
    Threads::Threads)
