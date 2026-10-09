// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// A within-family chain, written as a type: 12 kHz -> 32 kHz is 8/3, which
// the coverage matrix factors as up-by-2 then 4/3 (PLAN.md 3.3), each stage
// designed at its own rate (the design divisors, 3.1). The caller names
// the stages; nothing here is looked up from a rate pair (D12). Runs a
// 1 kHz tone through the chain in blocks, reports the exact latency and
// the MACs per output, and drains the tail with flush().
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "tap/sr/rational/rational.h"

int main() {
    using namespace tap::sr::rational;
    constexpr double      in_hz    = 12000.0;
    constexpr std::size_t channels = 2;

    chain<up_2, ratio_4_3> to_32k(channels); // economy: 3/8 of the lower rate flat, 70 dB
    static_assert(decltype(to_32k)::k_up == 8 && decltype(to_32k)::k_down == 3);
    static_assert(decltype(to_32k)::k_divisors[0] == tap::dsp::exact_ratio{1, 1}); // up_2 at the lowest rate
    static_assert(decltype(to_32k)::k_divisors[1] == tap::dsp::exact_ratio{2, 1}); // 4/3 at twice it

    const auto lat = to_32k.latency_output_frames();
    std::printf(
        "12 kHz -> 32 kHz as up_2 . ratio_4_3: latency %llu/%llu output frames = %.3f ms, %.2f MACs per output\n",
        static_cast<unsigned long long>(lat.num), static_cast<unsigned long long>(lat.den),
        1000.0 * to_32k.latency_seconds(in_hz * 8.0 / 3.0), to_32k.macs_per_output());

    // A 1 kHz tone, 300 frames at a time; outputs_for() sizes every block.
    constexpr std::size_t block = 300;
    std::vector<float>    in(block * channels), out;
    std::size_t           written = 0;
    for (std::size_t b = 0; b < 4; ++b) {
        for (std::size_t n = 0; n < block; ++n) {
            const double v =
                0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(b * block + n) / in_hz);
            for (std::size_t c = 0; c < channels; ++c) {
                in[n * channels + c] = static_cast<float>(v);
            }
        }
        out.resize((written + to_32k.outputs_for(block)) * channels);
        written += to_32k.process(in.data(), block, out.data() + written * channels);
    }
    out.resize((written + to_32k.flush_output_frames()) * channels);
    written += to_32k.flush(out.data() + written * channels); // end of stream
    std::printf("%zu input frames -> %zu output frames (the last %zu from flush)\n", 4 * block, written,
                to_32k.flush_output_frames());
    return 0;
}
