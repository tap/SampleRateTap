# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
#
# Extracts the first ```cpp block of a README into a translation unit:
# its #include lines hoisted to the top, the rest inside main() after a
# prelude that declares the free names the snippet uses (the caller's
# buffers and counts), so a quick start that stops compiling is a red
# check (<engine>.Family.ReadmeQuickStartCompiles) and not an audit finding.
#   cmake -DREADME=<path> -DPRELUDE=<path or ""> -DOUT=<path> -P readme_snippet.cmake
cmake_minimum_required(VERSION 3.24)
foreach(_var README OUT)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "${_var} is required")
    endif()
endforeach()
file(READ ${README} _text)
string(FIND "${_text}" "```cpp\n" _start)
if(_start LESS 0)
    message(FATAL_ERROR "${README}: no ```cpp block")
endif()
math(EXPR _start "${_start} + 7")
string(SUBSTRING "${_text}" ${_start} -1 _rest)
string(FIND "${_rest}" "\n```" _end)
if(_end LESS 0)
    message(FATAL_ERROR "${README}: the ```cpp block is not closed")
endif()
string(SUBSTRING "${_rest}" 0 ${_end} _snippet)
set(_includes "")
set(_body "")
string(REPLACE ";" "\;" _snippet "${_snippet}")
string(REPLACE "\n" ";" _lines "${_snippet}")
foreach(_line IN LISTS _lines)
    if(_line MATCHES "^#include")
        string(APPEND _includes "${_line}\n")
    else()
        string(APPEND _body "    ${_line}\n")
    endif()
endforeach()
set(_prelude "")
if(PRELUDE AND EXISTS "${PRELUDE}")
    file(READ ${PRELUDE} _prelude)
endif()
file(WRITE ${OUT} "// Generated from ${README} by tests/family/readme_snippet.cmake; do not edit.\n${_includes}\n${_prelude}\nint main() {\n${_body}    return 0;\n}\n")
message(STATUS "${README}: quick start extracted to ${OUT}")
