# The C ABI shared library (see tap_sr_bridge_capi.h). Built standalone by the
# notebooks' ctypes bridge:
#   cmake -S capi -B build_capi && cmake --build build_capi
# or from the top level with -DTAP_SR_BUILD_CAPI=ON.
cmake_minimum_required(VERSION 3.24)

if(NOT TARGET tap_sr_bridge)
    project(tap_sr_bridge_capi_standalone LANGUAGES CXX)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/ratiotap)
endif()

# Hidden by default, the C entry points exported by the source's visibility
# pragma: the dynamic symbol table is exactly the 17 tap_sr_bridge_*
# functions (no template or inline C++ leaks into it).
add_library(tap_sr_bridge_capi SHARED tap_sr_bridge_capi.cpp)
target_link_libraries(tap_sr_bridge_capi PRIVATE tap::sr::bridge)
set_target_properties(tap_sr_bridge_capi PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON)
