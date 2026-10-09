add_executable(multirate_chain multirate_chain.cpp)
target_link_libraries(multirate_chain PRIVATE
    tap::sr::rational
    tap_sr_rational_warnings)
