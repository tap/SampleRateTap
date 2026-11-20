// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (examples/down8_process_up8.cpp); see ../README.md.
// Down by 8, process at the low rate, up by 8: one clock, synchronous.
// 48 kHz stereo in, 6 kHz in the middle, 48 kHz out. Today's FIR stages.
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tap/sr/rational/rational.h>

namespace {
    using namespace tap::sr::rational;

    constexpr std::size_t k_ch    = 2;   // the channel count is the type's (0.7.0)
    constexpr std::size_t k_block = 384; // 8 ms at 48 kHz: a multiple of 24, see below

    // The coverage matrix's chains for 1/8 and 8/1 in the 48 kHz family:
    // ↓3 · 3/4 · ↓2 and ↑2 · 4/3 · ↑3, each stage designed at its own rate.
    using down_by_8 = down_3_ratio_3_4_down_2<float, k_ch>; // 48 -> 6 kHz
    using up_by_8   = up_2_ratio_4_3_up_3<float, k_ch>;     //  6 -> 48 kHz

    // Whatever runs at 6 kHz: here a one-pole low-pass per channel.
    struct low_rate_processor {
        float state[k_ch] = {};
        void  process(float* x, std::size_t frames) noexcept {
            for (std::size_t f = 0; f < frames; ++f) {
                for (std::size_t c = 0; c < k_ch; ++c) {
                    float& y = state[c];
                    y += 0.2f * (x[f * k_ch + c] - y);
                    x[f * k_ch + c] = y;
                }
            }
        }
    };
} // namespace

int main() {
    // Setup: every allocation and every filter design happens here.
    // (A build without exceptions asks down_by_8::validate() first.)
    down_by_8          down; // profile::economy(): 70 dB, flat to 3/8 of 6 kHz
    up_by_8            up;
    low_rate_processor fx;

    // A block that is a multiple of 24 input frames keeps every stage on
    // its period: 384 in -> exactly 48 at 6 kHz -> exactly 384 out, every
    // call. (Any other size works too; the counts then vary by a frame or
    // two per call and outputs_for() states each one exactly.)
    std::vector<float> in(k_block * k_ch), low(down.outputs_for(k_block) * k_ch), out(k_block * k_ch);

    // The round trip's latency, exactly, in 48 kHz frames: the decimator's
    // (in 6 kHz frames, times 8) plus the interpolator's.
    const tap::dsp::exact_ratio latency =
        down.latency_output_frames() * tap::dsp::exact_ratio{8, 1} + up.latency_output_frames();
    std::printf("round-trip latency: %llu/%llu frames = %.3f ms at 48 kHz\n",
                static_cast<unsigned long long>(latency.num), static_cast<unsigned long long>(latency.den),
                latency.value() / 48.0);
    std::printf("MACs per output frame per channel: down %.1f (at 6 kHz), up "
                "%.1f (at 48 kHz)\n",
                down.macs_per_output(), up.macs_per_output());

    // The audio callback: noexcept, allocation-free.
    double phase = 0.0;
    for (int block = 0; block < 250; ++block) { // 2 s
        for (std::size_t f = 0; f < k_block; ++f, phase += 2.0 * std::numbers::pi * 997.0 / 48000.0) {
            in[f * k_ch] = in[f * k_ch + 1] = static_cast<float>(0.5 * std::sin(phase));
        }
        const std::size_t n_low = down.process(in.data(), k_block, low.data()); // 48
        fx.process(low.data(), n_low);
        const std::size_t n_out = up.process(low.data(), n_low, out.data()); // 384
        if (n_low != k_block / 8 || n_out != k_block) {
            std::printf("block %d: %zu low, %zu out\n", block, n_low, n_out);
            return 1;
        }
    }
    std::printf("every block: %zu frames in, %zu at 6 kHz, %zu out\n", k_block, k_block / 8, k_block);
    return 0;
}
