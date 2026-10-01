/// @file design.h
/// @brief Quality profiles and the stage design of the rational engine, over DspTap's nyquist.h.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "tap/dsp/kaiser.h"
#include "tap/dsp/nyquist.h"
#include "tap/sr/rational/ratio.h"

namespace tap::sr::rational {

    // ANCHOR: rational_profile
    /// Quality profile: bridge's four names (R5), as (stopband attenuation A
    /// in dB, passband edge p as a fraction of the chain's LOWEST rate
    /// r_min). The edges are bridge's four at 48 kHz, stated as fractions
    /// from day one so that one profile serves a stage at every rate and a
    /// chain that composes bridge and rational at one name has one
    /// (f_pass, A) throughout.
    ///
    /// | profile       | A      | p = f_pass / r_min | at r_min = 48 kHz |
    /// |---------------|--------|--------------------|-------------------|
    /// | super_economy |  70 dB | 1/3                | 16 kHz            |
    /// | economy       |  70 dB | 3/8                | 18 kHz            |
    /// | balanced      |  70 dB | 19/48              | 19 kHz            |
    /// | transparent   | 120 dB | 5/12               | 20 kHz            |
    ///
    /// A stage's stopband edge is r - f_pass at its lower rate r (the
    /// Nyquist structure's symmetric transition, R4), which is the coverage
    /// rule's bound with equality (PLAN.md 2.1(a)). economy is the default,
    /// per the speed-first charter; the tap counts each profile buys are
    /// design_stage's (measured in PLAN.md 2.3, pinned by test_design.cpp).
    struct profile {
        double passband_frac     = 3.0 / 8.0; ///< f_pass / r_min, in (0, 1/2)
        double stopband_atten_db = 70.0;      ///< stopband target, dB

        /// The speed-first default: 70 dB, 3/8 of the lowest rate (18 kHz at 48).
        static constexpr profile economy() noexcept { return {}; }
        /// Voice/comms tier: 70 dB, 1/3 (16 kHz at 48). Never a default.
        static constexpr profile super_economy() noexcept {
            return {.passband_frac = 1.0 / 3.0, .stopband_atten_db = 70.0};
        }
        /// 70 dB, flat to 19/48 (19 kHz at 48).
        static constexpr profile balanced() noexcept {
            return {.passband_frac = 19.0 / 48.0, .stopband_atten_db = 70.0};
        }
        /// Pristine tier: 120 dB, 5/12 (20 kHz at 48).
        static constexpr profile transparent() noexcept {
            return {.passband_frac = 5.0 / 12.0, .stopband_atten_db = 120.0};
        }

        /// The passband edge in Hz for a chain whose lowest rate is r_min.
        constexpr double passband_hz(double lowest_rate_hz) const noexcept { return passband_frac * lowest_rate_hz; }
    };
    // ANCHOR_END: rational_profile

    /// The spec margin every stage design carries: the worst stopband
    /// response on nyquist_worst_stopband_db's grid is at or below
    /// -(A + k_margin_db), bridge's ">= 1 dB margin on a fine grid".
    inline constexpr double k_design_margin_db = 1.0;

    /// Validates a profile: finite, 0 < p < 1/2, A > 0. Throws
    /// std::invalid_argument otherwise (construction time; R9).
    inline void validate(const profile& p) {
        if (!(std::isfinite(p.passband_frac) && std::isfinite(p.stopband_atten_db)) || p.passband_frac <= 0.0
            || p.passband_frac >= 0.5 || p.stopband_atten_db <= 0.0) {
            throw std::invalid_argument("tap::sr::rational::design_stage: bad profile");
        }
    }

    // ANCHOR: rational_design
    /// Designs the one Nyquist filter of a stage at ratio R (R2, R4): a
    /// k_band-th-band Kaiser-windowed sinc at the stage's composite rate,
    /// length N = 2 m k_band - 1 with the smallest m that meets the
    /// stopband with the margin (search_nyquist_m), the centre tap exactly
    /// 1, every k_band-th tap from it exactly 0, every polyphase branch at
    /// DC gain 1 (sum L), as nyquist.h's contract states. The passband
    /// fraction is of the stage's lower rate, which is what the designer
    /// takes, because the composite rate is k_band times it for every
    /// ratio of the charter (ratio_traits).
    ///
    /// The up and down designs of one band are the same prototype (a
    /// Nyquist filter is its own transpose up to the gain convention), so
    /// design_stage<ratio<2, 1>> and design_stage<ratio<1, 2>> return the
    /// same vector bit for bit; test_design.cpp pins it.
    ///
    /// Construction-time code (runtime double, off the audio path);
    /// allocates and searches. Throws std::invalid_argument for a bad
    /// profile and std::runtime_error when no length up to the search
    /// bound meets the spec. M2 pins the searched m per (ratio, profile)
    /// so that a deployed stage's length is a number in a table, not a
    /// search; until then this is the search.
    template <rational_ratio R>
    std::vector<double> design_stage(const profile& p) {
        validate(p);
        constexpr std::size_t band = ratio_traits<R>::k_band;
        const std::size_t     m =
            tap::dsp::search_nyquist_m(band, p.passband_frac, p.stopband_atten_db, k_design_margin_db);
        if (m == 0) {
            throw std::runtime_error("tap::sr::rational::design_stage: no length meets the profile's spec");
        }
        std::vector<double> h(tap::dsp::nyquist_length(band, m));
        tap::dsp::design_nyquist(h, band, tap::dsp::kaiser_beta(p.stopband_atten_db));
        return h;
    }
    // ANCHOR_END: rational_design

    /// The group delay of design_stage<R>'s filter in samples at the
    /// composite rate: (N - 1) / 2, an integer (R7, PLAN.md 2.5).
    inline std::size_t stage_group_delay_composite(const std::vector<double>& h) noexcept {
        return h.empty() ? 0 : (h.size() - 1) / 2;
    }

} // namespace tap::sr::rational
