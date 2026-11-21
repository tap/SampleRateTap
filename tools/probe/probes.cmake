# The size and stack probes of one engine (scripts/ceilings.py), built with
# the ratchet's workloads (TAP_SR_BUILD_ICOUNT_BENCH) in the same tree and
# configuration, so the ceilings describe the code the counts do. Per format
# (f32, q15, q31), from the engine's probe_main.cpp:
#
#   tap_sr_<engine>_probe_<fmt>  the size probe: no stdio, linked with a map
#                                (<binary>.map), whose input sections give
#                                the probe's own translation unit apart
#                                from the runtime
#   tap_sr_<engine>_stack_<fmt>  the stack probe: prints each call's painted
#                                high-water; on the bare-metal GCC legs also
#                                compiled with -fstack-usage
#                                -fcallgraph-info=su, whose frames and call
#                                graph give each call's static worst path
#
# Neither name matches the ratchet's tap_sr_<engine>_icount_ glob.
#
# tap_sr_add_probes(<engine> <source> <libraries>... [SUFFIX <s>]
#                   [DEFINITIONS <definitions>...]) adds one set; a second
# set of the same engine takes a SUFFIX on both names
# (tap_sr_<engine>_probe<s>_<fmt>, tap_sr_<engine>_stack<s>_<fmt>: rational's
# 4th-band pair, SUFFIX 4) and the DEFINITIONS that select what it holds.
# scripts/ceilings.py lists each engine's sets (PROBE_SETS).
function(tap_sr_add_probes engine source)
    cmake_parse_arguments(PARSE_ARGV 2 _probe "" "SUFFIX" "DEFINITIONS")
    set(_libs ${_probe_UNPARSED_ARGUMENTS})
    foreach(_fmt IN ITEMS f32:0 q15:1 q31:2)
        string(REPLACE ":" ";" _parts "${_fmt}")
        list(GET _parts 0 _name)
        list(GET _parts 1 _type)

        set(_size tap_sr_${engine}_probe${_probe_SUFFIX}_${_name})
        add_executable(${_size} ${source})
        target_compile_definitions(${_size} PRIVATE TAP_SR_PROBE_TYPE=${_type} TAP_SR_PROBE_STACK=0
                                   ${_probe_DEFINITIONS})
        target_link_options(${_size} PRIVATE "LINKER:-Map=$<TARGET_FILE:${_size}>.map")
        target_link_libraries(${_size} PRIVATE ${_libs})

        set(_stack tap_sr_${engine}_stack${_probe_SUFFIX}_${_name})
        add_executable(${_stack} ${source})
        target_compile_definitions(${_stack} PRIVATE TAP_SR_PROBE_TYPE=${_type} TAP_SR_PROBE_STACK=1
                                   ${_probe_DEFINITIONS})
        target_include_directories(${_stack} PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
        # The bare-metal GCC legs only (the M33's is the one measured): a
        # hosted GCC's compile database feeds clang-tidy, whose clang front
        # end rejects -fcallgraph-info.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND TAP_SR_BARE_METAL)
            target_compile_options(${_stack} PRIVATE -fstack-usage -fcallgraph-info=su)
        endif()
        target_link_libraries(${_stack} PRIVATE ${_libs})
    endforeach()
endfunction()
