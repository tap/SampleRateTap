# The family's warning policy for the comparison tooling's own sources (the
# shims and the engines' comparison harnesses): the engines' flags, with
# -Werror whenever any engine's WERROR option is on, so the CI gates that
# build this tooling treat it as they treat the engines. Third-party sources
# (resample.c, the fetched headers) never take it.
if(NOT TARGET tap_sr_cmp_warnings)
    add_library(tap_sr_cmp_warnings INTERFACE)
    target_compile_options(tap_sr_cmp_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->)
    if(TAP_SR_ASYNC_WERROR OR TAP_SR_BRIDGE_WERROR OR TAP_SR_RATIONAL_WERROR)
        target_compile_options(tap_sr_cmp_warnings INTERFACE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>
            $<$<CXX_COMPILER_ID:MSVC>:/WX>)
    endif()
endif()
