// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Deterministic fixed workloads for the instruction-count ratchet (PLAN.md
// section 5): one scenario per binary, selected at compile time because
// bare-metal targets have no argv. The qemu plugin counts the whole run,
// construction included; the streaming loop is sized to dominate (2 s of
// stereo at the input rate in 32-frame blocks, bridge's shape), and one
// scenario measures construction alone. The checksum both defeats
// dead-code elimination and pins cross-run determinism.
//
// TAP_SR_RATIONAL_SC: 0 up2_float_eco, 1 down2_float_eco, 2 up3_float_eco,
// 3 down3_float_eco, 4 up2_q15_eco, 5 down2_q15_eco, 6 up3_q15_eco,
// 7 down3_q15_eco, 8 down2_down2_q15_eco, 9 up2_float_tr, 10 down2_float_tr,
// 11 construct_q15_eco.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

#include "tap/sr/rational/rational.h"

namespace {

    namespace rat = tap::sr::rational;

    constexpr std::size_t k_channels = 2;
    constexpr std::size_t k_block    = 32;
    constexpr double      k_rate_in  = 48000.0; // the input rate the sine is drawn at

    template <typename S>
    S make_sample(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return tap::dsp::detail::round_sat<S>(v * static_cast<double>(std::numeric_limits<S>::max()));
        }
    }

    // Interleaved sine block, precomputed so libm's sin() stays out of the
    // measured loop (on the soft-double targets it would dominate).
    template <typename S>
    std::vector<S> sine_block(std::size_t frames) {
        std::vector<S> out(frames * k_channels);
        const double   w = 2.0 * std::numbers::pi * 997.0 / k_rate_in;
        for (std::size_t i = 0; i < frames; ++i) {
            const S v = make_sample<S>(0.5 * std::sin(w * static_cast<double>(i)));
            for (std::size_t c = 0; c < k_channels; ++c) {
                out[i * k_channels + c] = v;
            }
        }
        return out;
    }

    /// Streams 2 s of the input rate through a converter (a stage or a
    /// chain) in k_block-frame blocks; returns the checksum.
    template <typename Conv>
    double stream(Conv& conv) {
        using sample = typename Conv::sample;
        // 0.25 s of input, cycled block-aligned (12000 % 32 == 0: the seam
        // repeats identically every cycle).
        const auto            input   = sine_block<sample>(12000);
        constexpr std::size_t out_cap = k_block * Conv::k_up / Conv::k_down + Conv::k_up + 2;
        std::vector<sample>   out(out_cap * k_channels);
        double                sink   = 0.0;
        std::size_t           off    = 0;
        const std::size_t     blocks = 2 * static_cast<std::size_t>(k_rate_in) / k_block;
        for (std::size_t b = 0; b < blocks; ++b) {
            const std::size_t made = conv.process(input.data() + off, k_block, out.data());
            if (made > out_cap) {
                return std::numeric_limits<double>::quiet_NaN(); // poisons the checksum
            }
            off += k_block * k_channels;
            if (off + k_block * k_channels > input.size()) {
                off = 0;
            }
            sink += static_cast<double>(out[0]) + static_cast<double>(made);
        }
        return sink;
    }

    template <typename S, rat::rational_ratio R>
    double stage_workload(const rat::profile& p) {
        rat::basic_stage<S, R> conv(k_channels, p);
        return stream(conv);
    }

    double run() {
        [[maybe_unused]] const rat::profile eco = rat::profile::economy();
        [[maybe_unused]] const rat::profile tr  = rat::profile::transparent();
#if TAP_SR_RATIONAL_SC == 0
        return stage_workload<float, rat::up_2>(eco);
#elif TAP_SR_RATIONAL_SC == 1
        return stage_workload<float, rat::down_2>(eco);
#elif TAP_SR_RATIONAL_SC == 2
        return stage_workload<float, rat::up_3>(eco);
#elif TAP_SR_RATIONAL_SC == 3
        return stage_workload<float, rat::down_3>(eco);
#elif TAP_SR_RATIONAL_SC == 4
        return stage_workload<std::int16_t, rat::up_2>(eco);
#elif TAP_SR_RATIONAL_SC == 5
        return stage_workload<std::int16_t, rat::down_2>(eco);
#elif TAP_SR_RATIONAL_SC == 6
        return stage_workload<std::int16_t, rat::up_3>(eco);
#elif TAP_SR_RATIONAL_SC == 7
        return stage_workload<std::int16_t, rat::down_3>(eco);
#elif TAP_SR_RATIONAL_SC == 8
        rat::down_2_down_2<std::int16_t> conv(k_channels, eco);
        return stream(conv);
#elif TAP_SR_RATIONAL_SC == 9
        return stage_workload<float, rat::up_2>(tr);
#elif TAP_SR_RATIONAL_SC == 10
        return stage_workload<float, rat::down_2>(tr);
#else
        // Construction alone: the Q15 by-4 chain's two designs (pinned
        // lengths, no search) and quantized tables, then one output frame.
        rat::down_2_down_2<std::int16_t> conv(k_channels, eco);
        std::int16_t                     in[4 * k_channels]  = {};
        std::int16_t                     out[2 * k_channels] = {};
        const std::size_t                made                = conv.process(in, 4, out);
        return static_cast<double>(made) + static_cast<double>(conv.template stage<0>().taps())
               + static_cast<double>(conv.template stage<1>().taps());
#endif
    }

} // namespace

int main() {
    const double checksum = run();
    const bool   ok       = checksum == checksum; // NaN check
    // The marker line is part of the count. Under static musl (Hexagon)
    // printf copies the format string's literal runs with memcpy, whose path
    // depends on the string's word alignment: pinning the alignment makes the
    // line cost the same wherever the rest of the image lands (the family
    // PLAN.md, section 7; bridge's icount_main.cpp). Byte-stable from the
    // first baseline (R12).
    alignas(64) static constexpr char k_done_fmt[] = "RATIONAL_ICOUNT_DONE ok=%d checksum=%.17g\n";
    std::printf(k_done_fmt, ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
