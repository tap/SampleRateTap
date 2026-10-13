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
function(tap_sr_add_probes engine source)
    foreach(_fmt IN ITEMS f32:0 q15:1 q31:2)
        string(REPLACE ":" ";" _parts "${_fmt}")
        list(GET _parts 0 _name)
        list(GET _parts 1 _type)

        set(_size tap_sr_${engine}_probe_${_name})
        add_executable(${_size} ${source})
        target_compile_definitions(${_size} PRIVATE TAP_SR_PROBE_TYPE=${_type} TAP_SR_PROBE_STACK=0)
        target_link_options(${_size} PRIVATE "LINKER:-Map=$<TARGET_FILE:${_size}>.map")
        target_link_libraries(${_size} PRIVATE ${ARGN})

        set(_stack tap_sr_${engine}_stack_${_name})
        add_executable(${_stack} ${source})
        target_compile_definitions(${_stack} PRIVATE TAP_SR_PROBE_TYPE=${_type} TAP_SR_PROBE_STACK=1)
        target_include_directories(${_stack} PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
        # The bare-metal GCC legs only (the M33's is the one measured): a
        # hosted GCC's compile database feeds clang-tidy, whose clang front
        # end rejects -fcallgraph-info.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND TAP_SR_BARE_METAL)
            target_compile_options(${_stack} PRIVATE -fstack-usage -fcallgraph-info=su)
        endif()
        target_link_libraries(${_stack} PRIVATE ${ARGN})
    endforeach()
endfunction()
