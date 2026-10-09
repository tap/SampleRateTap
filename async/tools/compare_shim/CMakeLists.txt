# ctypes shims over r8brain-free-src and SpeexDSP for
# notebooks/asrc_comparison.ipynb (neither has a maintained Python binding to
# lean on, unlike libsamplerate and soxr). Comparison-only: they never touch
# the library. The pins are shared with bench/compare and bench/icount via
# cmake/r8brain.cmake and cmake/speexdsp.cmake, so every leg measures the
# same engines.
include(${PROJECT_SOURCE_DIR}/cmake/r8brain.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/speexdsp.cmake)

add_library(tap_sr_async_r8b_shim SHARED tap_sr_async_r8b_shim.cpp)
target_compile_features(tap_sr_async_r8b_shim PRIVATE cxx_std_20)
target_link_libraries(tap_sr_async_r8b_shim PRIVATE tap_sr_async_r8brain tap_sr_async_warnings)
set_target_properties(tap_sr_async_r8b_shim PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(tap_sr_async_speex_shim SHARED tap_sr_async_speex_shim.cpp)
target_compile_features(tap_sr_async_speex_shim PRIVATE cxx_std_20)
# Both builds in one shim; the source includes the header once per build
# under its own prefix, so it takes the include path and no definitions.
target_include_directories(tap_sr_async_speex_shim SYSTEM PRIVATE
    ${speexdsp_SOURCE_DIR}/include/speex)
target_link_libraries(tap_sr_async_speex_shim PRIVATE
    tap_sr_async_speex_fixed tap_sr_async_speex_float tap_sr_async_warnings)
set_target_properties(tap_sr_async_speex_shim PROPERTIES POSITION_INDEPENDENT_CODE ON)
