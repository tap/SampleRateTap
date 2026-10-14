// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// The worst single call (the roadmap's A6): the most instructions one
// pull() executes, over a stream built to carry every expensive event a
// pull can meet. The counting plugin reports the largest count between the
// markers around each pull (tools/probe/call_marker.h), and scripts/icount.py
// gates that figure (scenarios callmax_*) two-sided like a total.
//
// The stream: two virtual clocks, the input 500 ppm fast, paced by
// support/two_clock_sim.h's rule in one thread (push while the input's
// virtual time has not passed the output's, pull otherwise), 16-frame blocks
// on both sides, the size the simulator shows the servo settling at (the
// Threads test's file comment). Its pulls meet:
//
// - the fill: silence while the ring primes, then the fade-in
// - the servo's promotions: acquiring, then locked
// - whole-sample slips: at +500 ppm the fractional delay wraps every 2000
//   output frames or so
// - ring wraps: the ring is a few thousand frames, the stream far longer
// - a hard resync and its splice: at 2 s the producer bursts 3000 frames
//   unpaced, past the high watermark, and the next pull resynchronizes
//
// then 2 s more. ok=1 only if the stream locked, resynchronized exactly once
// and never underran, so a workload that stopped meeting those events fails
// instead of quietly measuring an easier stream.
//
// TAP_SR_ASYNC_SC_TYPE: 0 = float, 1 = Q15, 2 = Q31
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

#include "call_marker.h"
#include "tap/sr/async/converter.h"

namespace {

    template <typename S>
    S make_sample(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return tap::sr::async::detail::round_sat<S>(v * static_cast<double>(std::numeric_limits<S>::max()));
        }
    }

    constexpr double      k_fs_out  = 48000.0;
    constexpr double      k_fs_in   = k_fs_out * (1.0 + 500e-6);
    constexpr std::size_t k_ch      = 2;
    constexpr std::size_t k_block   = 16;
    constexpr std::size_t k_burst   = 3000;
    constexpr double      k_burst_s = 2.0;
    constexpr double      k_end_s   = 4.0;

    template <typename S>
    [[gnu::noinline]] std::size_t measured_pull(tap::sr::async::basic_converter<S>& c, S* out) noexcept {
        tap_sr_call_begin();
        const std::size_t made = c.pull(out, k_block);
        tap_sr_call_end();
        return made;
    }

    template <typename S>
    double run() {
        tap::sr::async::config cfg;
        cfg.channels = k_ch;
        tap::sr::async::basic_converter<S> asrc(cfg);

        // 0.25 s of a 997 Hz tone at the input rate, cycled block-aligned
        // (12000 % 16 == 0); precomputed so libm stays out of the stream.
        std::vector<S> input(12000 * k_ch);
        const double   w = 2.0 * std::numbers::pi * 997.0 / k_fs_in;
        for (std::size_t i = 0; i < 12000; ++i) {
            const S v = make_sample<S>(0.5 * std::sin(w * static_cast<double>(i)));
            for (std::size_t c = 0; c < k_ch; ++c) {
                input[i * k_ch + c] = v;
            }
        }
        const std::vector<S> burst(k_burst * k_ch, make_sample<S>(0.0));
        std::vector<S>       out(k_block * k_ch);

        std::uint64_t pushed  = 0;
        std::uint64_t pulled  = 0;
        std::size_t   off     = 0;
        bool          bursted = false;
        double        sink    = 0.0;
        while (static_cast<double>(pulled) < k_end_s * k_fs_out) {
            if (!bursted && static_cast<double>(pulled) >= k_burst_s * k_fs_out) {
                asrc.push(burst.data(), k_burst); // not virtual time: the stall is the consumer's
                bursted = true;
            }
            const double t_in  = static_cast<double>(pushed) / k_fs_in;
            const double t_out = static_cast<double>(pulled) / k_fs_out;
            if (t_in <= t_out) {
                asrc.push(input.data() + off, k_block);
                pushed += k_block;
                off += k_block * k_ch;
                if (off + k_block * k_ch > input.size()) {
                    off = 0;
                }
            }
            else {
                sink += static_cast<double>(measured_pull(asrc, out.data()));
                sink += static_cast<double>(out[0]);
                pulled += k_block;
            }
        }
        const auto s = asrc.status();
        if (s.state != tap::sr::async::converter_state::locked || s.resyncs != 1 || s.underruns != 0) {
            return std::numeric_limits<double>::quiet_NaN(); // poisons the checksum: the events did not happen
        }
        return sink;
    }

} // namespace

int main() {
#if TAP_SR_ASYNC_SC_TYPE == 0
    const double checksum = run<float>();
#elif TAP_SR_ASYNC_SC_TYPE == 1
    const double checksum = run<std::int16_t>();
#else
    const double checksum = run<std::int32_t>();
#endif
    const bool ok = checksum == checksum; // NaN check
    // The marker line is part of the count; its format string's alignment is
    // pinned as icount_main.cpp's is (the family PLAN.md, section 7).
    alignas(64) static constexpr char k_done_fmt[] = "SRT_ICOUNT_DONE ok=%d checksum=%.17g\n";
    std::printf(k_done_fmt, ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
