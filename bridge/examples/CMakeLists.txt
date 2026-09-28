add_executable(bluetooth_bridge bluetooth_bridge.cpp)
target_link_libraries(bluetooth_bridge PRIVATE
    tap::ratio
    srt_headers
    tap_ratio_warnings)
