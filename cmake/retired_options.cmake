# D7 tripwire (docs/MONOREPO_PLAN.md): the options the tap::sr monorepo
# migration retired fail the configure loudly. CMake only warns about an
# unused -D, so a stale -DSRT_WERROR=ON would otherwise drop a gate silently.
# Included by the root and, when configured on their own, by both engines.
foreach(_retired IN ITEMS
        SRT_BUILD_TESTS SRT_BUILD_EXAMPLES SRT_BUILD_CAPI SRT_BUILD_ICOUNT_BENCH
        SRT_BUILD_BENCHMARKS SRT_BUILD_COMPARE_BENCH SRT_BUILD_COMPARE_SHIM
        SRT_ICOUNT_COMPARE SRT_WERROR SRT_BARE_METAL
        TAP_RATIO_BUILD_TESTS TAP_RATIO_BUILD_EXAMPLES TAP_RATIO_BUILD_CAPI
        TAP_RATIO_BUILD_ICOUNT_BENCH TAP_RATIO_WERROR TAP_RATIO_BARE_METAL)
    if(DEFINED ${_retired} OR DEFINED CACHE{${_retired}})
        message(FATAL_ERROR
            "${_retired} was retired by the tap::sr monorepo migration; "
            "the family options are TAP_SR_* (docs/MONOREPO_PLAN.md D7, D9)")
    endif()
endforeach()
unset(_retired)
