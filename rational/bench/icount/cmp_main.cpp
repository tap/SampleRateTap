// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Deterministic fixed workload for the cross-resampler instruction-count
// comparison (docs/COMPARISON.md). Same shape as icount_main.cpp but the
// engine is selected at compile time: rational runs its converter, and
// libsamplerate / r8brain-free-src / SpeexDSP run their own process() at the
// same ratio, so the comparison is engine-vs-engine.
//
// These binaries are intentionally named cmp_rational_icount_* so the
// ratchet (scripts/icount.py, glob tap_sr_rational_icount_*) never sees
// them: competitor instruction counts are measured once and recorded in
// docs/COMPARISON.md, not gated.
//
// TAP_SR_RATIONAL_CMP_RATIO:  0 = up 2 (48 -> 96 kHz), 1 = down 2 (96 -> 48),
//                   2 = 3/2 (32 -> 48), 3 = 2/3 (48 -> 32)
// TAP_SR_RATIONAL_CMP_ENGINE: 0 = rational economy, float; 1 = rational
//                   economy, Q15; 2 = rational transparent, float;
//                   3 = libsamplerate SRC_SINC_MEDIUM_QUALITY (the
//                   economy-matched row: its FASTEST droops at the edge);
//                   4 = libsamplerate SRC_SINC_BEST_QUALITY
//                   (transparent-matched); 5 = r8brain 70 dB at a 16 % band
//                   (economy-matched); 6 = r8brain 120 dB at a 10 % band
//                   (transparent-matched); 7 = SpeexDSP FIXED_POINT build on
//                   Q15 samples; 8 = SpeexDSP FLOATING_POINT, both at
//                   TAP_SR_RATIONAL_CMP_SPEEX_Q (2 is the float build's
//                   economy-matched quality, 3 the fixed-point build's, 9
//                   transparent-matched, 10 its best).
//                   bench/compare/bench_compare.cpp states the matching rule.
//
// TAP_SR_RATIONAL_CMP_SECONDS (default 2) sets the workload length in
// seconds of input. Every count includes one-time construction, so CMake
// builds each engine at 2 s and 4 s: the difference is the steady-state cost
// of 2 s of audio, the remainder is construction (docs/COMPARISON.md
// reports both, per output frame).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

#include "tap/dsp/sample_traits.h"
#if TAP_SR_RATIONAL_CMP_ENGINE <= 2
#include "tap/sr/rational/rational.h"
#elif TAP_SR_RATIONAL_CMP_ENGINE <= 4
#include <samplerate.h>
#elif TAP_SR_RATIONAL_CMP_ENGINE <= 6
#include <memory>

#include <CDSPResampler.h>
#else
#include <speex_resampler.h>
#endif

namespace {

    constexpr std::size_t k_ch    = 2;
    constexpr std::size_t k_block = 32;
#ifndef TAP_SR_RATIONAL_CMP_SECONDS
#define TAP_SR_RATIONAL_CMP_SECONDS 2
#endif
#if TAP_SR_RATIONAL_CMP_RATIO == 0
    constexpr double                  k_rate_in  = 48000.0;
    [[maybe_unused]] constexpr double k_rate_out = 96000.0; // rational's ratio is its type
#elif TAP_SR_RATIONAL_CMP_RATIO == 1
    constexpr double                  k_rate_in  = 96000.0;
    [[maybe_unused]] constexpr double k_rate_out = 48000.0;
#elif TAP_SR_RATIONAL_CMP_RATIO == 2
    constexpr double                  k_rate_in  = 32000.0;
    [[maybe_unused]] constexpr double k_rate_out = 48000.0;
#else
    constexpr double                  k_rate_in  = 48000.0;
    [[maybe_unused]] constexpr double k_rate_out = 32000.0;
#endif
    // The 0.25 s fixture, cycled (every rate here divides by 128, so the
    // fixture is a whole number of 32-frame blocks and the seam repeats).
    constexpr std::size_t k_in_frames = static_cast<std::size_t>(k_rate_in) / 4;
    constexpr std::size_t k_blocks    = TAP_SR_RATIONAL_CMP_SECONDS * static_cast<std::size_t>(k_rate_in) / k_block;

    template <typename S>
    S to_sample(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return tap::dsp::detail::round_sat<S>(v * static_cast<double>(std::numeric_limits<S>::max()));
        }
    }

    // Interleaved 997 Hz stereo sine at the input rate; precomputed so libm's
    // sin() stays out of the measured loop (as icount_main.cpp does).
    template <typename S>
    std::vector<S> sine_input() {
        std::vector<S> out(k_in_frames * k_ch);
        const double   w = 2.0 * std::numbers::pi * 997.0 / k_rate_in;
        for (std::size_t i = 0; i < k_in_frames; ++i) {
            for (std::size_t c = 0; c < k_ch; ++c) {
                out[i * k_ch + c] = to_sample<S>(0.5 * std::sin(w * static_cast<double>(i)));
            }
        }
        return out;
    }

    template <typename S>
    const S* block(const std::vector<S>& input, std::size_t& pos) {
        if (pos + k_block > k_in_frames) {
            pos = 0;
        }
        const S* p = input.data() + pos * k_ch;
        pos += k_block;
        return p;
    }

#if TAP_SR_RATIONAL_CMP_ENGINE <= 2

    namespace rat = tap::sr::rational;
#if TAP_SR_RATIONAL_CMP_RATIO == 0
    using ratio_t = rat::up_2;
#elif TAP_SR_RATIONAL_CMP_RATIO == 1
    using ratio_t = rat::down_2;
#elif TAP_SR_RATIONAL_CMP_RATIO == 2
    using ratio_t = rat::ratio_3_2;
#else
    using ratio_t = rat::ratio_2_3;
#endif

    double run() {
#if TAP_SR_RATIONAL_CMP_ENGINE == 1
        using sample = std::int16_t;
#else
        using sample = float;
#endif
#if TAP_SR_RATIONAL_CMP_ENGINE == 2
        const rat::profile prof = rat::profile::transparent();
#else
        const rat::profile prof = rat::profile::economy();
#endif
        rat::basic_converter<sample, ratio_t> conv(k_ch, prof);
        const auto                            input = sine_input<sample>();
        std::size_t                           pos   = 0;
        std::vector<sample>                   out((conv.outputs_for(k_block) + 2) * k_ch);
        double                                sink = 0.0;
        for (std::size_t b = 0; b < k_blocks; ++b) {
            if (conv.process(block(input, pos), k_block, out.data()) > 0) {
                sink += static_cast<double>(out[0]);
            }
        }
        return sink;
    }

#elif TAP_SR_RATIONAL_CMP_ENGINE <= 4

    double run() {
#if TAP_SR_RATIONAL_CMP_ENGINE == 3
        constexpr int k_converter = SRC_SINC_MEDIUM_QUALITY;
#else
        constexpr int k_converter = SRC_SINC_BEST_QUALITY;
#endif
        int        err = 0;
        SRC_STATE* src = src_new(k_converter, k_ch, &err);
        if (src == nullptr) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const auto         input = sine_input<float>();
        std::size_t        pos   = 0;
        std::vector<float> out(4 * k_block * k_ch);
        double             sink = 0.0;
        for (std::size_t b = 0; b < k_blocks; ++b) {
            SRC_DATA d{};
            d.data_in       = block(input, pos);
            d.input_frames  = static_cast<long>(k_block);
            d.data_out      = out.data();
            d.output_frames = static_cast<long>(4 * k_block);
            d.src_ratio     = k_rate_out / k_rate_in;
            if (src_process(src, &d) != 0 || d.input_frames_used != static_cast<long>(k_block)) {
                src_delete(src);
                return std::numeric_limits<double>::quiet_NaN();
            }
            if (d.output_frames_gen > 0) {
                sink += static_cast<double>(out[0]);
            }
        }
        src_delete(src);
        return sink;
    }

#elif TAP_SR_RATIONAL_CMP_ENGINE <= 6

    // r8brain is mono per instance with double-precision I/O: one instance per
    // channel, float<->double (de)interleave counted, as any float caller pays.
    double run() {
#if TAP_SR_RATIONAL_CMP_ENGINE == 5
        constexpr double k_atten = 70.0, k_band = 16.0;
#else
        constexpr double k_atten = 120.0, k_band = 10.0;
#endif
        std::unique_ptr<r8b::CDSPResampler> rs[k_ch];
        for (auto& r : rs) {
            r = std::make_unique<r8b::CDSPResampler>(k_rate_in, k_rate_out, static_cast<int>(k_block), k_band, k_atten,
                                                     r8b::fprLinearPhase);
        }
        const auto          input = sine_input<float>();
        std::size_t         pos   = 0;
        std::vector<double> ch_in(k_block);
        std::vector<float>  out(8 * k_block * k_ch);
        double              sink = 0.0;
        for (std::size_t b = 0; b < k_blocks; ++b) {
            const float* in  = block(input, pos);
            int          got = 0;
            for (std::size_t c = 0; c < k_ch; ++c) {
                for (std::size_t i = 0; i < k_block; ++i) {
                    ch_in[i] = static_cast<double>(in[i * k_ch + c]);
                }
                double* op = nullptr;
                got        = rs[c]->process(ch_in.data(), static_cast<int>(k_block), op);
                for (int i = 0; i < got; ++i) {
                    out[static_cast<std::size_t>(i) * k_ch + c] = static_cast<float>(op[i]);
                }
            }
            if (got > 0) {
                sink += static_cast<double>(out[0]);
            }
        }
        return sink;
    }

#else

    // SpeexDSP, interleaved, at the quality the build names: the fixed-point
    // build on Q15 samples, the float build on float.
    double run() {
#if TAP_SR_RATIONAL_CMP_ENGINE == 7
        using sample = std::int16_t;
#else
        using sample = float;
#endif
        int   err = 0;
        auto* st  = speex_resampler_init(static_cast<spx_uint32_t>(k_ch), static_cast<spx_uint32_t>(k_rate_in),
                                         static_cast<spx_uint32_t>(k_rate_out), TAP_SR_RATIONAL_CMP_SPEEX_Q, &err);
        if (st == nullptr) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const auto          input = sine_input<sample>();
        std::size_t         pos   = 0;
        std::vector<sample> out(4 * k_block * k_ch);
        double              sink = 0.0;
        for (std::size_t b = 0; b < k_blocks; ++b) {
            spx_uint32_t in_len  = static_cast<spx_uint32_t>(k_block);
            spx_uint32_t out_len = static_cast<spx_uint32_t>(4 * k_block);
#if TAP_SR_RATIONAL_CMP_ENGINE == 7
            const int rc =
                speex_resampler_process_interleaved_int(st, block(input, pos), &in_len, out.data(), &out_len);
#else
            const int rc =
                speex_resampler_process_interleaved_float(st, block(input, pos), &in_len, out.data(), &out_len);
#endif
            if (rc != 0 || in_len != k_block) {
                speex_resampler_destroy(st);
                return std::numeric_limits<double>::quiet_NaN();
            }
            if (out_len > 0) {
                sink += static_cast<double>(out[0]);
            }
        }
        speex_resampler_destroy(st);
        return sink;
    }

#endif

} // namespace

int main() {
    const double checksum = run();
    const bool   ok       = checksum == checksum; // NaN check
    // The marker line is part of the count; its format string's alignment is
    // pinned for the same reason icount_main.cpp pins its own (the family
    // PLAN.md, section 7: under static musl the line's cost depends on where
    // the linker places the string).
    alignas(64) static constexpr char k_done_fmt[] = "RATIONAL_ICOUNT_DONE ok=%d checksum=%.17g\n";
    std::printf(k_done_fmt, ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
