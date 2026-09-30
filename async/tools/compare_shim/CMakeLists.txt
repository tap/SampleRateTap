# ctypes shim over r8brain-free-src for notebooks/asrc_comparison.ipynb (the
# notebook has no maintained Python binding to lean on, unlike libsamplerate
# and soxr). Comparison-only: it never touches the library. The pin is shared
# with bench/compare via cmake/r8brain.cmake, so both measure the same engine.
include(${PROJECT_SOURCE_DIR}/cmake/r8brain.cmake)

add_library(tap_sr_async_r8b_shim SHARED tap_sr_async_r8b_shim.cpp)
target_compile_features(tap_sr_async_r8b_shim PRIVATE cxx_std_20)
target_link_libraries(tap_sr_async_r8b_shim PRIVATE tap_sr_async_r8brain tap_sr_async_warnings)
set_target_properties(tap_sr_async_r8b_shim PROPERTIES POSITION_INDEPENDENT_CODE ON)
