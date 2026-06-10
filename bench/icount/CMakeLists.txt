# One binary per scenario: bare-metal targets have no argv, and per-binary
# instruction totals are what the ratchet compares.
set(_srt_icount_scenarios
    kernel_float:0:0
    kernel_q15:0:1
    kernel_q31:0:2
    pipeline_float:1:0
    pipeline_q15:1:1
    pipeline_q31:1:2)

foreach(_sc IN LISTS _srt_icount_scenarios)
    string(REPLACE ":" ";" _parts "${_sc}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _kind)
    list(GET _parts 2 _type)
    add_executable(srt_icount_${_name} icount_main.cpp)
    target_compile_definitions(srt_icount_${_name} PRIVATE
        SRT_SC_KIND=${_kind} SRT_SC_TYPE=${_type})
    target_link_libraries(srt_icount_${_name} PRIVATE
        SampleRateTap::SampleRateTap srt_warnings)
endforeach()
