cmake_minimum_required(VERSION 3.24)
# The bridge engine's channel rule as a compile-time test (A13, K3): a
# converter of zero channels must FAIL to compile and the compiler's output
# must carry the engine's message. Configured by
# bridge.Converter.ZeroChannelsFailToCompileWithTheMessage (../CMakeLists.txt),
# never by the root build. Static-library target type: no link, so a
# bare-metal toolchain serves too.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(tap_sr_bridge_compile_fail LANGUAGES CXX)

if(NOT DEFINED TAP_SR_BRIDGE_INCLUDE OR NOT DEFINED TAP_DSP_INCLUDE)
    message(FATAL_ERROR "TAP_SR_BRIDGE_INCLUDE and TAP_DSP_INCLUDE are required")
endif()

set(_includes "${TAP_SR_BRIDGE_INCLUDE}\;${TAP_DSP_INCLUDE}")
try_compile(_compiled ${CMAKE_BINARY_DIR}/zero_channels.dir
            ${CMAKE_CURRENT_SOURCE_DIR}/zero_channels.cpp
            CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${_includes}"
            CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
            OUTPUT_VARIABLE _out)
if(_compiled)
    message(FATAL_ERROR "zero_channels.cpp compiled; it must be rejected")
endif()
if(NOT _out MATCHES "tap::sr::bridge: a converter's channel count is at least 1")
    message(FATAL_ERROR "zero_channels.cpp failed to compile without the engine's message:\n${_out}")
endif()
message(STATUS "zero_channels.cpp: rejected with the engine's message")
