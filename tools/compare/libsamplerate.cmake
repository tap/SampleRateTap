# libsamplerate (Erik de Castro Lopo, BSD-2) built from its release tarball
# for the embedded comparison workloads (TAP_SR_ICOUNT_COMPARE), where the
# target has no system package; the host benchmarks take the distro package
# through pkg-config instead. Comparison-only: never linked into a library
# or its tests. The version matches the Ubuntu 24.04 package the host rows
# measure, so one engine serves every leg.
if(NOT TARGET samplerate)
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LIBSAMPLERATE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libsamplerate
        URL https://github.com/libsndfile/libsamplerate/releases/download/0.2.2/libsamplerate-0.2.2.tar.xz
        URL_HASH SHA256=3258da280511d24b49d6b08615bbe824d0cacc9842b0e4caf11c52cf2b043893)
    # 0.2.2's CMakeLists asks for a CMake older than 3.5, whose compatibility
    # CMake 4 removed (the CI runners' since 2026-11): its configure is told
    # 3.5's policies, and only its own, by a variable unset after it. Older
    # CMakes do not read the variable.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(libsamplerate)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()
