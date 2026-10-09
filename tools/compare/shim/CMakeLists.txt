# ctypes shims over r8brain-free-src, SpeexDSP and soxr for the family's
# comparison notebooks (async/notebooks/asrc_comparison.ipynb,
# bridge/notebooks/bridge_comparison.ipynb): r8brain and SpeexDSP have no
# maintained Python binding to lean on, and python-soxr exposes only soxr's
# five named recipes, not the custom quality spec the matched rows need.
# Comparison-only: they never touch a library. The pins are shared with every
# engine's bench/compare and bench/icount through ../r8brain.cmake and
# ../speexdsp.cmake, so every leg measures the same engines.
include(${CMAKE_CURRENT_LIST_DIR}/../r8brain.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../speexdsp.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../warnings.cmake)

add_library(tap_sr_cmp_r8b_shim SHARED tap_sr_cmp_r8b_shim.cpp)
target_compile_features(tap_sr_cmp_r8b_shim PRIVATE cxx_std_20)
target_link_libraries(tap_sr_cmp_r8b_shim PRIVATE tap_sr_cmp_r8brain tap_sr_cmp_warnings)
set_target_properties(tap_sr_cmp_r8b_shim PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(tap_sr_cmp_speex_shim SHARED tap_sr_cmp_speex_shim.cpp)
target_compile_features(tap_sr_cmp_speex_shim PRIVATE cxx_std_20)
# Both builds in one shim; the source includes the header once per build
# under its own prefix, so it takes the include path and no definitions.
target_include_directories(tap_sr_cmp_speex_shim SYSTEM PRIVATE
    ${speexdsp_SOURCE_DIR}/include/speex)
target_link_libraries(tap_sr_cmp_speex_shim PRIVATE
    tap_sr_cmp_speex_fixed tap_sr_cmp_speex_float tap_sr_cmp_warnings)
set_target_properties(tap_sr_cmp_speex_shim PROPERTIES POSITION_INDEPENDENT_CODE ON)

# soxr from the system package (pkg-config), as the host benchmarks take it.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SOXR REQUIRED IMPORTED_TARGET soxr)
add_library(tap_sr_cmp_soxr_shim SHARED tap_sr_cmp_soxr_shim.cpp)
target_compile_features(tap_sr_cmp_soxr_shim PRIVATE cxx_std_20)
target_link_libraries(tap_sr_cmp_soxr_shim PRIVATE PkgConfig::SOXR tap_sr_cmp_warnings)
set_target_properties(tap_sr_cmp_soxr_shim PROPERTIES POSITION_INDEPENDENT_CODE ON)
