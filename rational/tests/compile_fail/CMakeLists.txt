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

# case source ; the phrase the charter's static_assert message must carry
# One case per static_assert of the charter, each isolated: 5/1 fails the
# form, 4/2 and 2/2 the lowest terms (2/2 reaches no further), 1/1 passes
# both and fails L != M alone.
set(_cases
    "ratio_5_1.cpp|of the form 2\\^a \\* 3\\^b"
    "ratio_4_2.cpp|lowest terms"
    "ratio_2_2.cpp|lowest terms"
    "ratio_1_1.cpp|no conversion")
set(_checked 0)
foreach(_case IN LISTS _cases)
    string(REPLACE "|" ";" _parts "${_case}")
    list(GET _parts 0 _src)
    list(GET _parts 1 _phrase)
    try_compile(_compiled ${CMAKE_BINARY_DIR}/${_src}.dir
                ${CMAKE_CURRENT_SOURCE_DIR}/${_src}
                CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${TAP_SR_RATIONAL_INCLUDE}"
                CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
                OUTPUT_VARIABLE _out)
    if(_compiled)
        message(FATAL_ERROR "${_src} compiled; the charter must reject it (PLAN.md R1)")
    endif()
    if(NOT _out MATCHES "tap::sr::rational: ")
        message(FATAL_ERROR "${_src} failed to compile without the charter's message:\n${_out}")
    endif()
    if(NOT _out MATCHES "${_phrase}")
        message(FATAL_ERROR "${_src} failed to compile without the expected phrase '${_phrase}':\n${_out}")
    endif()
    math(EXPR _checked "${_checked} + 1")
    message(STATUS "${_src}: rejected with the charter's message")
endforeach()
if(NOT _checked EQUAL 4)
    message(FATAL_ERROR "expected 4 cases, checked ${_checked}")
endif()
