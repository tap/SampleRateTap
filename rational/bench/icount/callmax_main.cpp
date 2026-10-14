// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// The worst single call (the roadmap's A6): the most instructions one
// process() executes over the streaming workload icount_main.cpp counts in
// total (2 s of a 997 Hz tone at 48 kHz, 32-frame blocks, economy,
// stereo). The counting plugin reports the largest count between the
// markers around each call (tools/probe/call_marker.h), and
// scripts/icount.py gates that figure (scenarios callmax_*) two-sided like
// a total.
//
// A stage's calls differ by what a block carries: the extra output frame
// the phase schedule allows on some blocks, and the history compaction when
// a block runs out of the 64 frames of append slack (for a decimator, its M
// branch lines at once). Two seconds visits every phase many times over.
//
// TAP_SR_RATIONAL_SC: 0 up2_float, 1 down2_float, 2 up2_q15, 3 down2_q15
// (all at economy)
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

#include "call_marker.h"
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

    template <typename Conv>
    [[gnu::noinline]] std::size_t measured_process(Conv& c, const typename Conv::sample* in,
                                                   typename Conv::sample* out) noexcept {
        tap_sr_call_begin();
        const std::size_t made = c.process(in, k_block, out);
        tap_sr_call_end();
        return made;
    }

    template <typename S, rat::rational_ratio R>
    double run() {
        rat::basic_stage<S, R> conv(k_channels, rat::profile::economy());
        using conv_t = decltype(conv);

        // 0.25 s of input, cycled block-aligned, as icount_main.cpp's stream.
        std::vector<S> input(12000 * k_channels);
        const double   w = 2.0 * std::numbers::pi * 997.0 / k_rate_in;
        for (std::size_t i = 0; i < 12000; ++i) {
            const S v = make_sample<S>(0.5 * std::sin(w * static_cast<double>(i)));
            for (std::size_t c = 0; c < k_channels; ++c) {
                input[i * k_channels + c] = v;
            }
        }
        constexpr std::size_t out_cap = k_block * conv_t::k_up / conv_t::k_down + conv_t::k_up + 2;
        std::vector<S>        out(out_cap * k_channels);

        double            sink   = 0.0;
        std::size_t       off    = 0;
        const std::size_t blocks = 2 * static_cast<std::size_t>(k_rate_in) / k_block;
        for (std::size_t b = 0; b < blocks; ++b) {
            const std::size_t made = measured_process(conv, input.data() + off, out.data());
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

} // namespace

int main() {
#if TAP_SR_RATIONAL_SC == 0
    const double checksum = run<float, rat::up_2>();
#elif TAP_SR_RATIONAL_SC == 1
    const double checksum = run<float, rat::down_2>();
#elif TAP_SR_RATIONAL_SC == 2
    const double checksum = run<std::int16_t, rat::up_2>();
#else
    const double checksum = run<std::int16_t, rat::down_2>();
#endif
    const bool ok = checksum == checksum; // NaN check
    // The marker line is part of the count; its format string's alignment is
    // pinned as icount_main.cpp's is (the family PLAN.md, section 7).
    alignas(64) static constexpr char k_done_fmt[] = "RATIONAL_ICOUNT_DONE ok=%d checksum=%.17g\n";
    std::printf(k_done_fmt, ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
