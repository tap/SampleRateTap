add_executable(multirate_chain multirate_chain.cpp)
target_link_libraries(multirate_chain PRIVATE
    tap::sr::rational
    tap_sr_rational_warnings)

# The low-band round trip (docs/X4_X8_PLAN.md 6.6): down_by16, a 4096-point
# STFT at 3 kHz, the change back through up_by16. DspTap's real FFT, which
# the dependency rule allows (tap::dsp is the one dependency).
add_executable(low_band_fft low_band_fft.cpp)
target_link_libraries(low_band_fft PRIVATE
    tap::sr::rational
    tap_sr_rational_warnings)
