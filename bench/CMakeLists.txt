include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(srt_bench bench_asrc.cpp)
target_link_libraries(srt_bench PRIVATE
    SampleRateTap::SampleRateTap
    srt_warnings
    benchmark::benchmark_main)
