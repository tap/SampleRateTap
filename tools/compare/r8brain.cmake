# r8brain-free-src (Aleksey Vaneev, MIT) for the family's resampler
# comparisons only (async/docs/COMPARISON.md, bridge/docs/COMPARISON.md):
# included by each engine's bench/compare and bench/icount
# (TAP_SR_ICOUNT_COMPARE) and by tools/compare/shim, so the host benchmarks,
# the embedded counts and the notebooks all measure the same pinned engine.
# Never linked into a library or its tests.
#
# Upstream's last tag (version-6.5) predates the current 7.x line, so this is a
# commit pin (commits are immutable; tags can move): master at r8bbase.h
# R8B_VERSION "7.6" (7.5 until 2026-10; 7.6's one change is faster filter
# calculation — new log/exp approximations and a reoptimized bessel0I —
# which is construction, not the audio path). Stock configuration: Ooura FFT
# (no IPP/PFFFT), double precision internally.
if(NOT TARGET tap_sr_cmp_r8brain)
    include(FetchContent)
    FetchContent_Declare(
        r8brain
        GIT_REPOSITORY https://github.com/avaneev/r8brain-free-src.git
        GIT_TAG cb2abb9977efe2471979b380ed95daa56ab4fdb9 # 7.6
        # Header-only upstream with no CMake project: fetch, don't add_subdirectory.
        SOURCE_SUBDIR do-not-add)
    FetchContent_MakeAvailable(r8brain)

    add_library(tap_sr_cmp_r8brain INTERFACE)
    # SYSTEM: third-party headers stay out of our -Wconversion/-Wshadow gate.
    target_include_directories(tap_sr_cmp_r8brain SYSTEM INTERFACE ${r8brain_SOURCE_DIR})
    # The filter cache's std::mutex. Bare-metal targets have no threads; the
    # icount workloads supply a single-threaded stand-in there instead.
    if(NOT TAP_SR_BARE_METAL)
        find_package(Threads REQUIRED)
        target_link_libraries(tap_sr_cmp_r8brain INTERFACE Threads::Threads)
    endif()
endif()
