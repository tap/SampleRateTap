# The C ABI shared library (see tap_sr_rational_capi.h). Built standalone by the
# notebooks' ctypes binding:
#   cmake -S capi -B build_capi && cmake --build build_capi
# or from the top level with -DTAP_SR_BUILD_CAPI=ON.
cmake_minimum_required(VERSION 3.24)

if(NOT TARGET tap_sr_rational)
    project(tap_sr_rational_capi_standalone LANGUAGES CXX)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/rational)
endif()

# Hidden by default, the C entry points exported by TAP_SR_RATIONAL_API on the
# header's declarations (dllexport on Windows while building): the dynamic
# symbol table is exactly the 23 tap_sr_rational_* functions, no template or
# inline C++ leaking into it, pinned by rational.Family.ExportedSymbolsArePinned
# (tests/family/symbols_rational.txt).
add_library(tap_sr_rational_capi SHARED tap_sr_rational_capi.cpp)
target_link_libraries(tap_sr_rational_capi PRIVATE tap::sr::rational tap_sr_rational_warnings)
target_compile_definitions(tap_sr_rational_capi PRIVATE TAP_SR_RATIONAL_CAPI_BUILDING)
set_target_properties(tap_sr_rational_capi PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON)
# On Windows the tests load the DLL from their own directory (no rpath):
# emit it beside the test executable (multi-config generators append the
# configuration subdirectory to both).
if(WIN32 AND TARGET tap_sr_rational_tests)
    set_target_properties(tap_sr_rational_capi PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../tests)
endif()
