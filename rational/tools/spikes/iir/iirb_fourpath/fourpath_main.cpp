// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Scratch probes (IIR-b, I12): the four-path stage against the half-band
// pair it replaces, on the shipping basic_iir_stage, counted by the QEMU
// plugin like icount_main.cpp (2 s of stereo at the input rate in 32-frame
// blocks, the same sine fixture). FP_SC selects the workload, FP_FMT the
// format (0 float, 1 Q15), FP_PROF the profile (0 super_economy, 1
// economy, 2 balanced).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

#include "tap/dsp/chain.h"
#include "tap/sr/rational/iir_stage.h"

namespace {

    namespace rat     = tap::sr::rational;
    constexpr auto np = rat::iir_phase::nonlinear;

    constexpr std::size_t k_channels = 2;
    constexpr std::size_t k_block    = 32;
    constexpr double      k_rate_in  = 48000.0;

#if FP_FMT == 0
    using S = float;
#else
    using S = std::int16_t;
#endif

    template <typename T>
    T make_sample(double v) {
        if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(v);
        }
        else {
            return tap::dsp::detail::round_sat<T>(v * static_cast<double>(std::numeric_limits<T>::max()));
        }
    }

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

    template <typename Conv>
    double stream(Conv& conv) {
        const auto            input   = sine_block(12000);
        constexpr std::size_t out_cap = k_block * Conv::k_up / Conv::k_down + Conv::k_up + 2;
        std::vector<S>        out(out_cap * k_channels);
        double                sink   = 0.0;
        std::size_t           off    = 0;
        const std::size_t     blocks = 2 * static_cast<std::size_t>(k_rate_in) / k_block;
        for (std::size_t b = 0; b < blocks; ++b) {
            const std::size_t made = conv.process(input.data() + off, k_block, out.data());
            if (made > out_cap) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            off += k_block * k_channels;
            if (off + k_block * k_channels > input.size()) {
                off = 0;
            }
            sink += static_cast<double>(out[0]) + static_cast<double>(made);
        }
        return sink;
    }

    template <typename R>
    using st = rat::basic_iir_stage<S, k_channels, R, np>;

    rat::profile prof() {
#if FP_PROF == 0
        return rat::profile::super_economy();
#elif FP_PROF == 1
        return rat::profile::economy();
#else
        return rat::profile::balanced();
#endif
    }

    double run() {
        const rat::profile p = prof();
        using tap::dsp::chain;
        using tap::dsp::exact_ratio;
#if FP_SC == 0 // the pair going down: down2 at d4, then down2 at d2
        chain<st<rat::down2>, st<rat::down2>> c(st<rat::down2>(p.relaxed({4, 1})), st<rat::down2>(p.relaxed({2, 1})));
        return stream(c);
#elif FP_SC == 1  // the four-path down4 at d2, in a chain of one
        chain<st<rat::down4>> c(st<rat::down4>(p.relaxed({2, 1})));
        return stream(c);
#elif FP_SC == 2  // the pair going up: up2 at d2, then up2 at d4
        chain<st<rat::up2>, st<rat::up2>> c(st<rat::up2>(p.relaxed({2, 1})), st<rat::up2>(p.relaxed({4, 1})));
        return stream(c);
#elif FP_SC == 3  // the four-path up4 at d2
        chain<st<rat::up4>> c(st<rat::up4>(p.relaxed({2, 1})));
        return stream(c);
#elif FP_SC == 4  // x8 down, three half-bands
        chain<st<rat::down2>, st<rat::down2>, st<rat::down2>> c(st<rat::down2>(p.relaxed({4, 1})),
                                                                st<rat::down2>(p.relaxed({2, 1})), st<rat::down2>(p));
        return stream(c);
#elif FP_SC == 5  // x8 down, the four-path at the top
        chain<st<rat::down4>, st<rat::down2>> c(st<rat::down4>(p.relaxed({2, 1})), st<rat::down2>(p));
        return stream(c);
#elif FP_SC == 6  // x16 down, four half-bands
        chain<st<rat::down2>, st<rat::down2>, st<rat::down2>, st<rat::down2>> c(
            st<rat::down2>(p.relaxed({8, 1})), st<rat::down2>(p.relaxed({4, 1})), st<rat::down2>(p.relaxed({2, 1})),
            st<rat::down2>(p));
        return stream(c);
#elif FP_SC == 7  // x16 down, the four-path in the middle
        chain<st<rat::down2>, st<rat::down4>, st<rat::down2>> c(st<rat::down2>(p.relaxed({8, 1})),
                                                                st<rat::down4>(p.relaxed({2, 1})), st<rat::down2>(p));
        return stream(c);
#elif FP_SC == 8  // x8 up, three half-bands
        chain<st<rat::up2>, st<rat::up2>, st<rat::up2>> c(st<rat::up2>(p), st<rat::up2>(p.relaxed({2, 1})),
                                                          st<rat::up2>(p.relaxed({4, 1})));
        return stream(c);
#elif FP_SC == 9  // x8 up, the four-path at the top
        chain<st<rat::up2>, st<rat::up4>> c(st<rat::up2>(p), st<rat::up4>(p.relaxed({2, 1})));
        return stream(c);
#elif FP_SC == 10 // x16 up, four half-bands
        chain<st<rat::up2>, st<rat::up2>, st<rat::up2>, st<rat::up2>> c(
            st<rat::up2>(p), st<rat::up2>(p.relaxed({2, 1})), st<rat::up2>(p.relaxed({4, 1})),
            st<rat::up2>(p.relaxed({8, 1})));
        return stream(c);
#elif FP_SC == 11 // x16 up, the four-path in the middle
        chain<st<rat::up2>, st<rat::up4>, st<rat::up2>> c(st<rat::up2>(p), st<rat::up4>(p.relaxed({2, 1})),
                                                          st<rat::up2>(p.relaxed({8, 1})));
        return stream(c);
#else             // the fixture and construction alone (the pair), one block
        chain<st<rat::down2>, st<rat::down2>> c(st<rat::down2>(p.relaxed({4, 1})), st<rat::down2>(p.relaxed({2, 1})));
        const auto                            input = sine_block(12000);
        std::vector<S>                        out(64 * k_channels);
        return static_cast<double>(c.process(input.data(), k_block, out.data()));
#endif
    }

} // namespace

int main() {
#if defined(__arm__) && defined(__ARM_FP)
    // The host's flush-to-zero (I9), as a firmware sets it at reset.
    __builtin_arm_set_fpscr(__builtin_arm_get_fpscr() | (1u << 24));
#endif
    const double                      checksum     = run();
    const bool                        ok           = checksum == checksum;
    alignas(64) static constexpr char k_done_fmt[] = "RATIONAL_ICOUNT_DONE ok=%d checksum=%.17g\n";
    std::printf(k_done_fmt, ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
