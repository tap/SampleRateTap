# The C ABI shared library (see ratio_capi.h). Built standalone by the
# notebooks' ctypes bridge:
#   cmake -S tools/capi -B build_capi && cmake --build build_capi
# or from the top level with -DTAP_RATIO_BUILD_CAPI=ON.
cmake_minimum_required(VERSION 3.24)

if(NOT TARGET tap_ratio)
    project(RatioTapCapi LANGUAGES CXX)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/ratiotap)
endif()

add_library(ratio_capi SHARED ratio_capi.cpp)
target_link_libraries(ratio_capi PRIVATE tap::ratio)
set_target_properties(ratio_capi PROPERTIES
    CXX_VISIBILITY_PRESET default
    POSITION_INDEPENDENT_CODE ON)
