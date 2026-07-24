// Deterministic fixed workloads for the instruction-count ratchet
// (PLAN.md section 7): every M7 optimization lever must move these numbers,
// measured, before it merges. One scenario per binary, selected at compile
// time because bare-metal targets have no argv. The qemu plugin counts the
// whole run including table construction; the streaming loop is sized to
// dominate. The checksum both defeats dead-code elimination and pins down
// cross-run determinism.
//
// RATIO_SC_DIR:     0 = up (44.1 -> 48), 1 = down (48 -> 44.1)
// RATIO_SC_TYPE:    0 = float, 1 = Q15, 2 = Q31
// RATIO_SC_PROFILE: 0 = economy, 1 = transparent
// RATIO_SC_CH:      channel count (default 2)
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

#include "tap/ratio/ratio.h"

namespace {

#ifndef RATIO_SC_CH
#define RATIO_SC_CH 2
#endif

    template <typename S>
    S make_sample(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return tap::dsp::detail::round_sat<S>(v * static_cast<double>(std::numeric_limits<S>::max()));
        }
    }

    // Interleaved sine block at the scenario's input rate; precomputed so
    // libm's sin() stays out of the measured loop (on the soft-double
    // targets it would otherwise dominate and mask lever improvements).
    template <typename S>
    std::vector<S> sine_block(std::size_t frames, std::size_t channels, double freq_hz, double rate, double amp) {
        std::vector<S> out(frames * channels);
        const double   w = 2.0 * std::numbers::pi * freq_hz / rate;
        for (std::size_t i = 0; i < frames; ++i) {
            const S v = make_sample<S>(amp * std::sin(w * static_cast<double>(i)));
            for (std::size_t c = 0; c < channels; ++c) {
                out[i * channels + c] = v;
            }
        }
        return out;
    }

    template <typename S>
    double run() {
        using tap::ratio::direction;
#if RATIO_SC_DIR == 0
        constexpr direction k_dir     = direction::up_to_48k;
        constexpr double    k_rate_in = 44100.0;
#else
        constexpr direction k_dir     = direction::down_to_44k1;
        constexpr double    k_rate_in = 48000.0;
#endif
#if RATIO_SC_PROFILE == 0
        const tap::ratio::profile k_prof = tap::ratio::profile::economy();
#else
        const tap::ratio::profile k_prof = tap::ratio::profile::transparent();
#endif
        constexpr std::size_t k_ch    = RATIO_SC_CH;
        constexpr std::size_t k_block = 32;

        tap::ratio::basic_converter<S, k_dir> conv(k_ch, k_prof);

        // 0.25 s of input, cycled block-aligned (12000 % 32 == 0, so the
        // waveform seam repeats identically every cycle: deterministic).
        const auto input = sine_block<S>(12000, k_ch, 997.0, k_rate_in, 0.5);

        // outputs_for(k_block) never exceeds ceil(32 * 160/147) + 1 = 36
        // frames in the up direction; 64 leaves inarguable headroom.
        std::vector<S> out(64 * k_ch);

        double            sink   = 0.0;
        std::size_t       off    = 0;
        const std::size_t blocks = 2 * static_cast<std::size_t>(k_rate_in) / k_block; // 2 s of virtual audio
        for (std::size_t b = 0; b < blocks; ++b) {
            const std::size_t made = conv.process(input.data() + off, k_block, out.data());
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
#if RATIO_SC_TYPE == 0
    const double checksum = run<float>();
#elif RATIO_SC_TYPE == 1
    const double checksum = run<std::int16_t>();
#else
    const double checksum = run<std::int32_t>();
#endif
    const bool ok = checksum == checksum; // NaN check
    std::printf("RATIO_ICOUNT_DONE ok=%d checksum=%.17g\n", ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
