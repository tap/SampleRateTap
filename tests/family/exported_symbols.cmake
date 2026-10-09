# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
#
# The C ABI's exported symbol set, pinned: the library's dynamic symbol
# table (nm) must be exactly the committed list of tap_sr_<engine>_* entry
# points, so a leaked C++ symbol (a dropped visibility preset, a non-static
# helper) or a stray entry point fails a test instead of shipping. Run by
# <engine>.Family.ExportedSymbolsArePinned on the ELF and Mach-O hosts:
#   cmake -DENGINE=<engine> -DLIBRARY=<path> -DEXPECTED=<list file> -DNM=<nm> -P exported_symbols.cmake
cmake_minimum_required(VERSION 3.24) # IN_LIST and the other policies a -P script needs
foreach(_var ENGINE LIBRARY EXPECTED NM)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "${_var} is required")
    endif()
endforeach()

# ELF: the dynamic table, defined symbols only. Mach-O: the global defined
# symbols (nm on macOS has no -D; the Mach-O export table is the global
# defined set of a library built with hidden visibility), with their
# leading underscore stripped.
if(APPLE)
    execute_process(COMMAND ${NM} -gU ${LIBRARY} OUTPUT_VARIABLE _nm RESULT_VARIABLE _rc)
else()
    execute_process(COMMAND ${NM} -D --defined-only ${LIBRARY} OUTPUT_VARIABLE _nm RESULT_VARIABLE _rc)
endif()
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${NM} failed on ${LIBRARY} (${_rc})")
endif()

# Toolchain bookkeeping that is not an entry point and not a leak.
set(_ignore _init _fini __mh_dylib_header _mh_dylib_header __dso_handle)

set(_found)
string(REPLACE "\n" ";" _lines "${_nm}")
foreach(_line IN LISTS _lines)
    # "<address> <type> <name>" (ELF: type T; Mach-O: T for text) or a
    # "<name>" with no address for undefined, which -D/-U already exclude.
    string(REGEX MATCH "^[0-9a-fA-F]* *([A-Za-z]) +([^ ]+)$" _m "${_line}")
    if(NOT _m)
        continue()
    endif()
    set(_type ${CMAKE_MATCH_1})
    set(_name ${CMAKE_MATCH_2})
    if(APPLE)
        string(REGEX REPLACE "^_" "" _name "${_name}")
    endif()
    if(_name IN_LIST _ignore)
        continue()
    endif()
    # Weak or data symbols in the table are leaks too: everything defined counts.
    list(APPEND _found "${_name}")
endforeach()
list(SORT _found)
list(REMOVE_DUPLICATES _found)

file(STRINGS ${EXPECTED} _expected)
list(SORT _expected)

set(_extra ${_found})
list(REMOVE_ITEM _extra ${_expected})
set(_missing ${_expected})
list(REMOVE_ITEM _missing ${_found})
list(LENGTH _found _n_found)
list(LENGTH _expected _n_expected)
if(_extra OR _missing)
    message(FATAL_ERROR "${ENGINE}: the exported symbol set is not the committed list (${EXPECTED}): "
                        "${_n_found} exported, ${_n_expected} expected; extra: [${_extra}]; missing: [${_missing}]")
endif()
message(STATUS "${ENGINE}: ${_n_found} exported symbols, exactly the committed list")
