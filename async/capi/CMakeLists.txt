# Hidden by default, the C entry points exported by TAP_SR_ASYNC_API on the
# header's declarations (dllexport on Windows while building): the dynamic
# symbol table is exactly the 14 tap_sr_async_* functions, no template or
# inline C++ leaking into it, pinned by async.Family.ExportedSymbolsArePinned
# (tests/family/symbols_async.txt).
add_library(tap_sr_async_capi SHARED tap_sr_async_capi.cpp)
target_link_libraries(tap_sr_async_capi PRIVATE tap::sr::async tap_sr_async_warnings)
target_compile_definitions(tap_sr_async_capi PRIVATE TAP_SR_ASYNC_CAPI_BUILDING)
set_target_properties(tap_sr_async_capi PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON)
# On Windows the tests load the DLL from their own directory (no rpath):
# emit it beside the test executable (multi-config generators append the
# configuration subdirectory to both).
if(WIN32 AND TARGET tap_sr_async_tests)
    set_target_properties(tap_sr_async_capi PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../tests)
endif()
