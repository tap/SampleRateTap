cmake_minimum_required(VERSION 3.24)
# The rational engine's charter as a compile-time test (PLAN.md R1, M1):
# each case must FAIL to compile and the compiler's output must carry the
# charter's message. Configured by rational.Ratio.ChartersFailToCompileWithTheMessage
# (../CMakeLists.txt), never by the root build. Static-library target type:
# no link, so a bare-metal toolchain serves too.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(tap_sr_rational_compile_fail LANGUAGES CXX)

if(NOT DEFINED TAP_SR_RATIONAL_INCLUDE)
    message(FATAL_ERROR "TAP_SR_RATIONAL_INCLUDE is required")
endif()

# case source ; the phrase the static_assert's message must carry
# A third field "dsp" marks a case refused by DspTap's own rule, whose
# message is DspTap's (no engine prefix).
if(TAP_SR_RATIONAL_COMPILE_FAIL_SET STREQUAL "channels")
    # A13: a stage's channel count is a compile-time constant of at least 1;
    # zero is the one value the type rejects (CHANNELS_PLAN.md K3).
    set(_cases "zero_channels.cpp|channel count is at least 1")
    set(_expected 1)
elseif(TAP_SR_RATIONAL_COMPILE_FAIL_SET STREQUAL "iir")
    # The recursive stages (docs/IIR_PLAN.md I2, I5, I12; X10): by 3 is
    # planned and refused with its name, by 4 was measured and does not
    # ship, the nonlinear variant has no exact delay path for the round-trip
    # scaffold (subband_delta's exact_delay_stage constraint) and no recursive
    # stage an exact latency for a chain, nor the guard band filter's
    # Chebyshev II form (I7: its phase is nonlinear).
    set(_cases
        "iir_ratio_up3.cpp|up3 / down3, the three-path structure, is planned"
        "iir_down4.cpp|four-path stage by 4 \\(up4, down4\\) was measured and does not ship"
        "iir_nonlinear_subband.cpp|exact_delay_stage|dsp"
        "iir_exact_latency.cpp|a chain's latency needs every stage to report latency_output_frames|dsp"
        "guard_band_exact_latency.cpp|a chain's latency needs every stage to report latency_output_frames|dsp")
    set(_expected 5)
else()
    # One case per static_assert of the charter, each isolated: 5/1 fails the
    # form, 4/2 and 2/2 the lowest terms (2/2 reaches no further), 1/1 passes
    # both and fails L != M alone.
    set(_cases
        "ratio_5_1.cpp|of the form 2\\^a \\* 3\\^b"
        "ratio_4_2.cpp|lowest terms"
        "ratio_2_2.cpp|lowest terms"
        "ratio_1_1.cpp|no conversion")
    set(_expected 4)
endif()
# Include directories as one escaped list: rational's, and DspTap's when a
# case reaches its headers (the channel case does, through stage.h).
set(_include_list "${TAP_SR_RATIONAL_INCLUDE}")
if(DEFINED TAP_DSP_INCLUDE)
    list(APPEND _include_list "${TAP_DSP_INCLUDE}")
endif()
string(REPLACE ";" "\;" _includes "${_include_list}")
set(_checked 0)
foreach(_case IN LISTS _cases)
    string(REPLACE "|" ";" _parts "${_case}")
    list(GET _parts 0 _src)
    list(GET _parts 1 _phrase)
    list(LENGTH _parts _nparts)
    set(_engine_message TRUE)
    if(_nparts GREATER 2)
        set(_engine_message FALSE)
    endif()
    try_compile(_compiled ${CMAKE_BINARY_DIR}/${_src}.dir
                ${CMAKE_CURRENT_SOURCE_DIR}/${_src}
                CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${_includes}"
                CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
                OUTPUT_VARIABLE _out)
    if(_compiled)
        message(FATAL_ERROR "${_src} compiled; it must be rejected")
    endif()
    if(_engine_message AND NOT _out MATCHES "tap::sr::rational: ")
        message(FATAL_ERROR "${_src} failed to compile without the engine's message:\n${_out}")
    endif()
    if(NOT _out MATCHES "${_phrase}")
        message(FATAL_ERROR "${_src} failed to compile without the expected phrase '${_phrase}':\n${_out}")
    endif()
    math(EXPR _checked "${_checked} + 1")
    message(STATUS "${_src}: rejected with the engine's message")
endforeach()
if(NOT _checked EQUAL _expected)
    message(FATAL_ERROR "expected ${_expected} cases, checked ${_checked}")
endif()
