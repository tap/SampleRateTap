/// @file design.h
/// @brief Direction, quality profiles, and prototype design for 44.1 <-> 48.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "tap/dsp/kaiser.h"

namespace tap::ratio {

    // ANCHOR: rt_direction
    /// Conversion direction — a compile-time parameter, per the charter: the
    /// two directions are different filters with different phase counts, and
    /// the speed-first mandate wants every table size known at compile time.
    /// A deployment needing both (e.g. a Bluetooth bridge) instantiates both.
    enum class direction {
        up_to_48k,   ///< 44.1 kHz -> 48 kHz, L/M = 160/147
        down_to_44k1 ///< 48 kHz -> 44.1 kHz, L/M = 147/160
    };

    /// Compile-time facts of one direction. L is the interpolation factor
    /// (and phase count and superblock period); M the decimation factor. The
    /// phase sequence phase(n) = (n * M) mod L visits every phase exactly
    /// once per superblock of L outputs, consuming exactly M input frames.
    template <direction D>
    struct ratio_traits;

    template <>
    struct ratio_traits<direction::up_to_48k> {
        static constexpr std::size_t k_phases         = 160;     ///< L
        static constexpr std::size_t k_decimation     = 147;     ///< M
        static constexpr double      k_input_rate_hz  = 44100.0;
        static constexpr double      k_output_rate_hz = 48000.0;
        /// Anti-image stopband edge: the output Nyquist. Images of baseband
        /// content land at 44100 - f >= 22.05 kHz — ultrasonic by arithmetic;
        /// the region above 24 kHz is what the filter must remove.
        static constexpr double k_stopband_edge_hz = 24000.0;
    };

    template <>
    struct ratio_traits<direction::down_to_44k1> {
        static constexpr std::size_t k_phases         = 147;     ///< L
        static constexpr std::size_t k_decimation     = 160;     ///< M
        static constexpr double      k_input_rate_hz  = 48000.0;
        static constexpr double      k_output_rate_hz = 44100.0;
        /// Anti-alias stopband edge: the output Nyquist. A 48 kHz source
        /// holds nothing above 24 kHz and aliasing maps f -> 44100 - f, so
        /// the entire possible alias landing zone is 20.1-22.05 kHz — nothing
        /// can fold below 20.1 kHz, arithmetically.
        static constexpr double k_stopband_edge_hz = 22050.0;
    };
    // ANCHOR_END: rt_direction

    // ANCHOR: rt_profile
    /// Quality profile. Two tiers behind one design path; taps-per-phase are
    /// pinned numbers from the M2 design spike (notebooks/design_spike.ipynb,
    /// verified by test_design.cpp): the minimal even counts whose Kaiser
    /// designs meet the stopband spec with >= 1 dB margin.
    ///
    /// | profile     | stopband | passband | taps down | taps up | measured worst stop |
    /// |-------------|----------|----------|-----------|---------|---------------------|
    /// | economy     |  70 dB   | 19 kHz   |  78       |  44     | -72.1 / -72.3 dB    |
    /// | transparent | 120 dB   | 20 kHz   | 184       |  96     | -121.4 / -121.1 dB  |
    ///
    /// economy is the default, per the speed-first charter: going down, every
    /// alias product is confined above 20.1 kHz by arithmetic (see
    /// ratio_traits), so its 70 dB stopband buys ultrasonic cleanliness at
    /// half the compute and storage of transparent — the relaxation trades
    /// nothing audible. transparent exists for pristine/offline use and for
    /// consumers who post-process the ultrasonic band.
    struct profile {
        double      passband_hz       = 19000.0; ///< edge of the flat passband
        double      stopband_atten_db = 70.0;    ///< prototype stopband target
        std::size_t taps_up_to_48k    = 44;      ///< taps per phase, 44.1 -> 48
        std::size_t taps_down_to_44k1 = 78;      ///< taps per phase, 48 -> 44.1

        /// The speed-first default: ~70 dB stopband, 19 kHz passband.
        static profile economy() noexcept { return {}; }

        /// Pristine tier: 120 dB stopband, flat to 20 kHz.
        static profile transparent() noexcept {
            return {.passband_hz       = 20000.0,
                    .stopband_atten_db = 120.0,
                    .taps_up_to_48k    = 96,
                    .taps_down_to_44k1 = 184};
        }

        template <direction D>
        std::size_t taps() const noexcept {
            return D == direction::up_to_48k ? taps_up_to_48k : taps_down_to_44k1;
        }
    };
    // ANCHOR_END: rt_profile

    // ANCHOR: rt_design
    /// Designs the direction's Kaiser-windowed sinc prototype at the
    /// L-times-oversampled rate: length L * taps, normalized so each
    /// polyphase branch has DC gain ~1 (sum(h) == L). Cutoff sits midway
    /// between the passband edge and the direction's stopband edge, exactly
    /// as the shared tap::dsp designer expects. Construction-time code per
    /// the family philosophy (runtime double, off the audio path); allocates.
    template <direction D>
    std::vector<double> design_prototype(const profile& p) {
        using traits = ratio_traits<D>;
        if (!(std::isfinite(p.passband_hz) && std::isfinite(p.stopband_atten_db)) || p.passband_hz <= 0.0
            || p.passband_hz >= traits::k_stopband_edge_hz || p.stopband_atten_db <= 0.0 || p.taps<D>() < 4) {
            throw std::invalid_argument("tap::ratio::design_prototype: bad profile");
        }
        std::vector<double> h(traits::k_phases * p.taps<D>());
        const double        cutoff_norm = (p.passband_hz + traits::k_stopband_edge_hz) / traits::k_input_rate_hz;
        tap::dsp::design_prototype(h, traits::k_phases, cutoff_norm, tap::dsp::kaiser_beta(p.stopband_atten_db));

        // Per-branch DC normalization: scale every polyphase branch so its
        // coefficient sum is exactly 1.0 in double. The raw windowed-sinc
        // leaves branch sums spread by the stopband leakage (~5e-6 at the
        // 70 dB tier); since the schedule visits branches with period L, that
        // spread would turn DC and low-frequency energy into an L-periodic
        // gain ripple — spurs at multiples of fs_out / L (~300 Hz spacing) at
        // the spread level. Normalizing kills those spurs identically, and
        // lets the row-sum-preserving quantization land every fixed-point row
        // on the format's unity exactly (the RBJ DC condition). The response
        // perturbation is at the spread's own level, far beneath each
        // profile's spec — re-verified by the spec sweep in test_design.cpp.
        for (std::size_t ph = 0; ph < traits::k_phases; ++ph) {
            double sum = 0.0;
            for (std::size_t t = 0; t < p.taps<D>(); ++t) {
                sum += h[t * traits::k_phases + ph];
            }
            const double gain = 1.0 / sum;
            for (std::size_t t = 0; t < p.taps<D>(); ++t) {
                h[t * traits::k_phases + ph] *= gain;
            }
        }
        return h;
    }
    // ANCHOR_END: rt_design

} // namespace tap::ratio
