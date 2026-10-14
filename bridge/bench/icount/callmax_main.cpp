// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// The worst single call (the roadmap's A6): the most instructions one
// process() executes over the streaming workload icount_main.cpp counts in
// total (2 s of a 997 Hz tone, 32-frame blocks, economy, stereo). The
// counting plugin reports the largest count between the markers around each
// call (tools/probe/call_marker.h), and scripts/icount.py gates that figure
// (scenarios callmax_*) two-sided like a total.
//
// A synchronous converter's calls differ by what a block carries: one more
// output frame when the phase schedule allows it (34 or 35 per 32 in going
// up, 29 or 30 going down), and the history compaction when a block runs
// out of the 64 frames of append slack. Two seconds visits every phase of
// the 147/160 schedule many times over, so the worst combination is in it.
//
// TAP_SR_BRIDGE_SC_DIR:  0 = up (44.1 -> 48), 1 = down (48 -> 44.1)
// TAP_SR_BRIDGE_SC_TYPE: 0 = float, 1 = Q15, 2 = Q31
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

#include "call_marker.h"
#include "tap/sr/bridge/ratio.h"

namespace {

    template <typename S>
    S make_sample(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return tap::dsp::detail::round_sat<S>(v * static_cast<double>(std::numeric_limits<S>::max()));
        }
    }

    using tap::sr::bridge::direction;
#if TAP_SR_BRIDGE_SC_DIR == 0
    constexpr direction k_dir     = direction::up_to_48k;
    constexpr double    k_rate_in = 44100.0;
#else
    constexpr direction k_dir     = direction::down_to_44k1;
    constexpr double    k_rate_in = 48000.0;
#endif
    constexpr std::size_t k_ch    = 2;
    constexpr std::size_t k_block = 32;

    template <typename S>
    [[gnu::noinline]] std::size_t measured_process(tap::sr::bridge::basic_converter<S, k_dir>& c, const S* in,
                                                   S* out) noexcept {
        tap_sr_call_begin();
        const std::size_t made = c.process(in, k_block, out);
        tap_sr_call_end();
        return made;
    }

    template <typename S>
    double run() {
        tap::sr::bridge::basic_converter<S, k_dir> conv(k_ch, tap::sr::bridge::profile::economy());

        // 0.25 s of input, cycled block-aligned, as icount_main.cpp's stream.
        std::vector<S> input(12000 * k_ch);
        const double   w = 2.0 * std::numbers::pi * 997.0 / k_rate_in;
        for (std::size_t i = 0; i < 12000; ++i) {
            const S v = make_sample<S>(0.5 * std::sin(w * static_cast<double>(i)));
            for (std::size_t c = 0; c < k_ch; ++c) {
                input[i * k_ch + c] = v;
            }
        }
        std::vector<S> out(64 * k_ch);

        double            sink   = 0.0;
        std::size_t       off    = 0;
        const std::size_t blocks = 2 * static_cast<std::size_t>(k_rate_in) / k_block;
        for (std::size_t b = 0; b < blocks; ++b) {
            const std::size_t made = measured_process(conv, input.data() + off, out.data());
            if (made > 64) {
                return std::numeric_limits<double>::quiet_NaN(); // poisons the checksum
            }
            off += k_block * k_ch;
            if (off + k_block * k_ch > input.size()) {
                off = 0;
            }
            sink += static_cast<double>(out[0]) + static_cast<double>(made);
        }
        return sink;
    }

} // namespace

int main() {
#if TAP_SR_BRIDGE_SC_TYPE == 0
    const double checksum = run<float>();
#elif TAP_SR_BRIDGE_SC_TYPE == 1
    const double checksum = run<std::int16_t>();
#else
    const double checksum = run<std::int32_t>();
#endif
    const bool ok = checksum == checksum; // NaN check
    // The marker line is part of the count; its format string's alignment is
    // pinned as icount_main.cpp's is (the family PLAN.md, section 7).
    alignas(64) static constexpr char k_done_fmt[] = "RATIO_ICOUNT_DONE ok=%d checksum=%.17g\n";
    std::printf(k_done_fmt, ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
