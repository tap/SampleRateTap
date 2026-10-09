// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
/// \file tap_sr_cmp_soxr_shim.cpp
/// \brief C entry points over soxr's custom quality spec, so the comparison
/// notebooks can measure soxr at a stated precision and passband edge — the
/// matched rows of bridge/docs/COMPARISON.md — through ctypes. python-soxr
/// exposes only the five named recipes. Build with TAP_SR_BUILD_COMPARE_SHIM=ON.
///
/// `recipe` >= 0 selects a named recipe (soxr.h: SOXR_QQ .. SOXR_VHQ and the
/// SOXR_*_BITQ values) and the other two knobs are ignored; `recipe` < 0 is
/// the custom spec: `precision` in bits (soxr accepts 16 and up) and
/// `passband_end` as a fraction of Nyquist. Both calls use the library's own
/// defaults for everything else (linear phase, stopband at Nyquist).
///
/// Errors surface as a negative return, never as an exception across the C
/// boundary.
#include <cstddef>

#include <soxr.h>

namespace {

    soxr_quality_spec_t spec(int recipe, double precision, double passband_end) {
        soxr_quality_spec_t q = soxr_quality_spec(recipe < 0 ? SOXR_HQ : static_cast<unsigned long>(recipe), 0);
        if (recipe < 0) {
            q.precision    = precision;
            q.passband_end = passband_end;
        }
        return q;
    }

} // namespace

extern "C" {

/// Convert `n_in` mono float frames at `src_hz` into at most `n_out` frames
/// at `dst_hz` through soxr_oneshot() (delay removed, tail flushed, the
/// library's documented offline usage). Returns the frames written, or -1 on
/// invalid arguments or a spec soxr rejects.
int tap_sr_cmp_soxr_oneshot(const float* in, int n_in, double src_hz, double dst_hz, int recipe, double precision,
                            double passband_end, float* out, int n_out) noexcept {
    if (in == nullptr || out == nullptr || n_in < 0 || n_out < 0 || src_hz <= 0.0 || dst_hz <= 0.0) {
        return -1;
    }
    const soxr_io_spec_t      io    = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
    const soxr_quality_spec_t q     = spec(recipe, precision, passband_end);
    std::size_t               idone = 0;
    std::size_t               odone = 0;
    const soxr_error_t        err   = soxr_oneshot(src_hz, dst_hz, 1, in, static_cast<std::size_t>(n_in), &idone, out,
                                                   static_cast<std::size_t>(n_out), &odone, &io, &q, nullptr);
    return err != nullptr ? -1 : static_cast<int>(odone);
}

/// Streaming delay in output frames (soxr_delay()) once the stream is primed:
/// a streaming soxr is created with the spec and fed one 4096-frame block of
/// silence, then asked. Returns a negative value on a spec soxr rejects.
double tap_sr_cmp_soxr_delay_frames(double src_hz, double dst_hz, int recipe, double precision,
                                    double passband_end) noexcept {
    if (src_hz <= 0.0 || dst_hz <= 0.0) {
        return -1.0;
    }
    const soxr_io_spec_t      io  = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
    const soxr_quality_spec_t q   = spec(recipe, precision, passband_end);
    soxr_error_t              err = nullptr;
    soxr_t                    s   = soxr_create(src_hz, dst_hz, 1, &err, &io, &q, nullptr);
    if (err != nullptr) {
        return -1.0;
    }
    static float zeros_in[4096]  = {};
    static float zeros_out[8192] = {};
    std::size_t  idone           = 0;
    std::size_t  odone           = 0;
    soxr_process(s, zeros_in, 4096, &idone, zeros_out, 8192, &odone);
    const double delay = soxr_delay(s);
    soxr_delete(s);
    return delay;
}

} // extern "C"
