# 4.2 check 1: an engine's link interface is exactly tap::dsp.
#   cmake -DENGINE=<engine> -DLINKS=<INTERFACE_LINK_LIBRARIES> -DEXPECT=tap::dsp -P this
if(NOT DEFINED LINKS OR NOT DEFINED EXPECT OR NOT DEFINED ENGINE)
    message(FATAL_ERROR "ENGINE, LINKS and EXPECT are required")
endif()
if(NOT "${LINKS}" STREQUAL "${EXPECT}")
    message(FATAL_ERROR "tap::sr::${ENGINE} links '${LINKS}'; the family rule allows exactly '${EXPECT}'")
endif()
message(STATUS "tap::sr::${ENGINE} links exactly ${EXPECT}")
