# Hidden by default, the C entry points exported by the source's visibility
# pragma: the dynamic symbol table is exactly the 14 tap_sr_async_* functions
# (no template or inline C++ leaks into it).
add_library(tap_sr_async_capi SHARED tap_sr_async_capi.cpp)
target_link_libraries(tap_sr_async_capi PRIVATE tap::sr::async tap_sr_async_warnings)
set_target_properties(tap_sr_async_capi PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON)
