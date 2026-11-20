// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0 (x4x8 plan, X3): scratch instruction counts of the x8 chains,
// down and up, at economy, in float and Q15, built and counted as the
// ratchet builds and counts rational's workloads (bench/icount/
// icount_main.cpp's shape: two interleaved channels, 32-frame blocks, a
// precomputed 997 Hz sine at a nominal 48 kHz input, cycled every 12000
// frames; the same toolchain files, Release, the same QEMU plugin).
//
// One scenario per binary (bare metal has no argv):
//   X0_CHAIN  0 down4 . down2 (ratio<1, 4>, down_2; X3's chain)
//             1 down_3_ratio_3_4_down_2 (the 48 family's matrix chain today)
//             2 down2 . down2 . down2 (three half-bands)
//             3 ratio_3_8_down_3 (the 44.1 family's matrix chain today)
//             4 up2 . up4 (up_2, ratio<4, 1>; X3's chain)
//             5 up_2_ratio_4_3_up_3 (the 48 family's matrix chain today)
//             6 up2 . up2 . up2 (three half-bands)
//             7 up_3_ratio_8_3 (the 44.1 family's matrix chain today)
//   X0_Q15    0 float, 1 Q15
//   X0_BLOCKS the number of 32-frame input blocks streamed
// Each chain is built twice, at 1000 and 3000 blocks; run_icount.py
// takes the difference of the two counts over the difference of the output
// frames each prints, so construction, the fixture and start-up cancel and
// what is left is the streaming loop per output frame (two channels).
// Not part of any build CI runs; see icount/README in ../README.md.
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
    constexpr double      k_rate_in  = 48000.0;

    template <typename S>
    S make_sample(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return tap::dsp::detail::round_sat<S>(v * static_cast<double>(std::numeric_limits<S>::max()));
        }
    }

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

    std::size_t g_frames = 0; // output frames produced, printed beside the marker

    template <typename Conv>
    double stream(Conv& conv) {
        using sample                  = typename Conv::sample;
        const auto            input   = sine_block<sample>(12000);
        constexpr std::size_t out_cap = k_block * Conv::k_up / Conv::k_down + Conv::k_up + 2;
        std::vector<sample>   out(out_cap * k_channels);
        double                sink = 0.0;
        std::size_t           off  = 0;
        for (std::size_t b = 0; b < X0_BLOCKS; ++b) {
            const std::size_t made = conv.process(input.data() + off, k_block, out.data());
            if (made > out_cap) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            g_frames += made;
            off += k_block * k_channels;
            if (off + k_block * k_channels > input.size()) {
                off = 0;
            }
            sink += static_cast<double>(out[0]) + static_cast<double>(made);
        }
        return sink;
    }

    template <typename S, rat::rational_ratio... Rs>
    double chain_workload(const rat::profile& p) {
        rat::basic_chain<S, k_channels, Rs...> conv(p);
        return stream(conv);
    }

#if X0_Q15
    using sample_t = std::int16_t;
#else
    using sample_t = float;
#endif

    double run() {
        const rat::profile eco = rat::profile::economy();
#if X0_CHAIN == 0
        return chain_workload<sample_t, rat::ratio<1, 4>, rat::down_2>(eco);
#elif X0_CHAIN == 1
        return chain_workload<sample_t, rat::down_3, rat::ratio_3_4, rat::down_2>(eco);
#elif X0_CHAIN == 2
        return chain_workload<sample_t, rat::down_2, rat::down_2, rat::down_2>(eco);
#elif X0_CHAIN == 3
        return chain_workload<sample_t, rat::ratio_3_8, rat::down_3>(eco);
#elif X0_CHAIN == 4
        return chain_workload<sample_t, rat::up_2, rat::ratio<4, 1>>(eco);
#elif X0_CHAIN == 5
        return chain_workload<sample_t, rat::up_2, rat::ratio_4_3, rat::up_3>(eco);
#elif X0_CHAIN == 6
        return chain_workload<sample_t, rat::up_2, rat::up_2, rat::up_2>(eco);
#else
        return chain_workload<sample_t, rat::up_3, rat::ratio_8_3>(eco);
#endif
    }

} // namespace

int main() {
    const double checksum = run();
    const bool   ok       = checksum == checksum;
    // The ratchet's marker format, so scripts/icount.py's measure() parses it.
    alignas(64) static constexpr char k_frames_fmt[] = "X0_FRAMES %lu\n";
    alignas(64) static constexpr char k_done_fmt[]   = "RATIONAL_ICOUNT_DONE ok=%d checksum=%.17g\n";
    std::printf(k_frames_fmt, static_cast<unsigned long>(g_frames));
    std::printf(k_done_fmt, ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
