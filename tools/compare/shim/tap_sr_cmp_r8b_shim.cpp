// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
/// \file tap_sr_cmp_r8b_shim.cpp
/// \brief C entry points over r8brain-free-src's CDSPResampler, so the
/// family's comparison notebooks (async/notebooks/asrc_comparison.ipynb,
/// bridge/notebooks/bridge_comparison.ipynb) can measure the real C++ engine
/// through ctypes. Build with TAP_SR_BUILD_COMPARE_SHIM=ON.
///
/// Two calls, both taking r8brain's own design knobs verbatim (transition
/// band in percent, stop-band attenuation in dB, linear or minimum phase):
/// a whole-buffer conversion through r8brain's oneshot() — which removes the
/// filter delay and flushes the tail, the library's documented offline
/// usage — and the streaming latency, i.e. the input frames r8brain
/// consumes before its first output frame (getInLenBeforeOutPos(0)).
///
/// Errors surface as a nonzero return / -1, never as an exception across
/// the C boundary.
#include <exception>
#include <vector>

#include <CDSPResampler.h>

namespace {

    constexpr int k_max_in_len = 4096; // oneshot() block size; any value works

    r8b::EDSPFilterPhaseResponse phase(int min_phase) {
        return min_phase != 0 ? r8b::fprMinPhase : r8b::fprLinearPhase;
    }

} // namespace

extern "C" {

/// Convert `n_in` mono float frames at `src_hz` into exactly `n_out` frames at
/// `dst_hz`. Returns 0 on success, -1 on invalid arguments or failure.
int tap_sr_cmp_r8b_oneshot(const float* in, int n_in, double src_hz, double dst_hz, double trans_band_pct,
                           double atten_db, int min_phase, float* out, int n_out) noexcept {
    if (in == nullptr || out == nullptr || n_in < 0 || n_out < 0 || src_hz <= 0.0 || dst_hz <= 0.0) {
        return -1;
    }
    try {
        r8b::CDSPResampler rs(src_hz, dst_hz, k_max_in_len, trans_band_pct, atten_db, phase(min_phase));
        // oneshot() takes a mutable pointer; never write through the caller's.
        std::vector<float> copy(in, in + n_in);
        rs.oneshot(copy.data(), n_in, out, n_out);
        return 0;
    }
    catch (const std::exception&) {
        return -1;
    }
}

/// Streaming latency: input frames consumed before the first output frame.
/// Returns -1 on invalid arguments or failure.
int tap_sr_cmp_r8b_latency_frames(double src_hz, double dst_hz, double trans_band_pct, double atten_db,
                                  int min_phase) noexcept {
    if (src_hz <= 0.0 || dst_hz <= 0.0) {
        return -1;
    }
    try {
        const r8b::CDSPResampler rs(src_hz, dst_hz, k_max_in_len, trans_band_pct, atten_db, phase(min_phase));
        return rs.getInLenBeforeOutPos(0);
    }
    catch (const std::exception&) {
        return -1;
    }
}

} // extern "C"
