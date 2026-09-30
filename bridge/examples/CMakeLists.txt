add_executable(bluetooth_bridge bluetooth_bridge.cpp)
target_link_libraries(bluetooth_bridge PRIVATE
    tap::sr::bridge
    tap_sr_async_headers
    tap_sr_bridge_warnings)
