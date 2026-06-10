add_library(srt_capi SHARED srt_capi.cpp)
target_link_libraries(srt_capi PRIVATE SampleRateTap::SampleRateTap srt_warnings)
set_target_properties(srt_capi PROPERTIES POSITION_INDEPENDENT_CODE ON)
