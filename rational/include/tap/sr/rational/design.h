/// @file design.h
/// @brief Quality profiles and the stage design of the rational engine, over DspTap's nyquist.h.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

#include "tap/dsp/chain.h"
#include "tap/dsp/kaiser.h"
#include "tap/dsp/nyquist.h"
#include "tap/sr/rational/ratio.h"

namespace tap::sr::rational {

    /// The pinned taps per branch of a named profile at one design divisor
    /// (profile::relaxed): m for the bands 2, 3, 4, 6, 8 of a stage whose
    /// passband is the profile's over `divisor` times its own lower rate.
    struct relaxed_pins {
        tap::dsp::exact_ratio      divisor;
        std::array<std::size_t, 5> taps_per_branch;
    };

    // ANCHOR: rational_relaxations
    /// The relaxation tables (tools/coverage/matrix.py `pins`, 2026-10-02;
    /// verified against the shipping designer by test_design.cpp): for each
    /// named profile, the pinned m per band at every design divisor a chain
    /// of the coverage matrix uses (PLAN.md 3.1). The 1/1 row is the M2
    /// table; a divisor above 1 relaxes the passband (a stage further up a
    /// chain, exact 2^a 3^b within a family); 147/160 is the one stage that
    /// runs below a 48-family r_min, at bridge's rate, tightened.
    inline constexpr std::array<relaxed_pins, 10> k_super_economy_relaxations = {{
        {{147, 160}, {11, 10, 10, 10, 10}},
        {{1, 1}, {9, 8, 8, 8, 8}},
        {{2, 1}, {7, 5, 5, 5, 5}},
        {{3, 1}, {7, 5, 5, 5, 5}},
        {{4, 1}, {7, 5, 5, 5, 5}},
        {{6, 1}, {4, 4, 4, 4, 4}},
        {{8, 1}, {4, 4, 4, 4, 4}},
        {{9, 1}, {3, 4, 4, 4, 4}},
        {{12, 1}, {3, 3, 3, 4, 4}},
        {{16, 1}, {3, 3, 3, 4, 4}},
    }};
    inline constexpr std::array<relaxed_pins, 10> k_economy_relaxations       = {{
        {{147, 160}, {15, 14, 14, 14, 14}},
        {{1, 1}, {11, 11, 11, 12, 12}},
        {{2, 1}, {7, 5, 5, 5, 5}},
        {{3, 1}, {7, 5, 5, 5, 5}},
        {{4, 1}, {7, 5, 5, 5, 5}},
        {{6, 1}, {4, 5, 4, 4, 4}},
        {{8, 1}, {4, 4, 4, 4, 4}},
        {{9, 1}, {4, 4, 4, 4, 4}},
        {{12, 1}, {3, 3, 3, 4, 4}},
        {{16, 1}, {3, 3, 3, 4, 4}},
    }};
    inline constexpr std::array<relaxed_pins, 10> k_balanced_relaxations      = {{
        {{147, 160}, {19, 19, 19, 20, 20}},
        {{1, 1}, {13, 13, 14, 14, 14}},
        {{2, 1}, {7, 5, 5, 5, 5}},
        {{3, 1}, {7, 5, 5, 5, 5}},
        {{4, 1}, {7, 5, 5, 5, 5}},
        {{6, 1}, {6, 5, 4, 4, 4}},
        {{8, 1}, {4, 4, 4, 4, 4}},
        {{9, 1}, {4, 4, 4, 4, 4}},
        {{12, 1}, {3, 3, 4, 4, 4}},
        {{16, 1}, {3, 3, 3, 4, 4}},
    }};
    inline constexpr std::array<relaxed_pins, 10> k_transparent_relaxations   = {{
        {{147, 160}, {45, 46, 46, 46, 46}},
        {{1, 1}, {31, 25, 25, 25, 25}},
        {{2, 1}, {10, 8, 13, 13, 13}},
        {{3, 1}, {9, 8, 11, 11, 12}},
        {{4, 1}, {9, 8, 8, 7, 10}},
        {{6, 1}, {9, 8, 7, 7, 10}},
        {{8, 1}, {9, 7, 7, 7, 10}},
        {{9, 1}, {8, 7, 7, 7, 10}},
        {{12, 1}, {8, 7, 7, 7, 10}},
        {{16, 1}, {7, 7, 7, 7, 7}},
    }};
    // ANCHOR_END: rational_relaxations

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
    ///
    /// The taps per branch m of each band's design are PINNED numbers (the
    /// M2 design spike, notebooks/design_spike.ipynb, 2026-10-02; verified
    /// by test_design.cpp against the shipping designer): the minimal m
    /// whose L-th-band design meets the stopband with >= 1 dB margin on a
    /// 16384-point grid (k_design_grid_points), N = 2 m B - 1:
    ///
    /// | profile       | A      | p     | m: B=2 | B=3 | B=4 | B=6 | B=8 | N: B=2 | B=3 | B=4 | B=6 | B=8 |
    /// |---------------|--------|-------|--------|-----|-----|-----|-----|--------|-----|-----|-----|-----|
    /// | super_economy |  70 dB | 1/3   | 9      | 8   | 8   | 8   | 8   | 35     | 47  | 63  | 95  | 127 |
    /// | economy       |  70 dB | 3/8   | 11     | 11  | 11  | 12  | 12  | 43     | 65  | 87  | 143 | 191 |
    /// | balanced      |  70 dB | 19/48 | 13     | 13  | 14  | 14  | 14  | 51     | 77  | 111 | 167 | 223 |
    /// | transparent   | 120 dB | 5/12  | 31     | 25  | 25  | 25  | 25  | 123    | 149 | 199 | 299 | 399 |
    ///
    /// Measured worst stopband -71.1 .. -72.8 dB at the 70 dB tiers and
    /// -121.1 .. -121.7 dB at transparent, passband ripple <= 0.0025 dB and
    /// <= 0.00001 dB (PLAN.md 2.3's candidates are 0.01 / 0.0001). A custom
    /// profile leaves its pins at 0 and design_stage searches instead.
    ///
    /// In a chain the profile is the CHAIN's: f_pass = p * r_min with r_min
    /// the chain's lowest rate, so a stage whose lower rate is r is designed
    /// at the fraction p * r_min / r of its own rate (PLAN.md 3.1, the
    /// stages further up a chain are shorter). relaxed(d) is that stage's
    /// profile: the passband over d, with the named profiles' pins at the
    /// divisors the coverage matrix uses (the relaxation tables above; a
    /// divisor without a row, or a custom profile, is searched).
    struct profile {
        double passband_frac     = 3.0 / 8.0; ///< f_pass / r_min, in (0, 1/2)
        double stopband_atten_db = 70.0;      ///< stopband target, dB
        /// Pinned taps per branch for the bands 2, 3, 4, 6, 8 (0: search).
        std::array<std::size_t, 5> taps_per_branch = {11, 11, 11, 12, 12};
        /// The pins at the other design divisors (relaxed()); empty for a
        /// custom profile.
        std::span<const relaxed_pins> relaxations = k_economy_relaxations;

        /// The speed-first default: 70 dB, 3/8 of the lowest rate (18 kHz at 48).
        static constexpr profile economy() noexcept { return {}; }
        /// Voice/comms tier: 70 dB, 1/3 (16 kHz at 48). Never a default.
        static constexpr profile super_economy() noexcept {
            return {.passband_frac     = 1.0 / 3.0,
                    .stopband_atten_db = 70.0,
                    .taps_per_branch   = {9, 8, 8, 8, 8},
                    .relaxations       = k_super_economy_relaxations};
        }
        /// 70 dB, flat to 19/48 (19 kHz at 48).
        static constexpr profile balanced() noexcept {
            return {.passband_frac     = 19.0 / 48.0,
                    .stopband_atten_db = 70.0,
                    .taps_per_branch   = {13, 13, 14, 14, 14},
                    .relaxations       = k_balanced_relaxations};
        }
        /// Pristine tier: 120 dB, 5/12 (20 kHz at 48).
        static constexpr profile transparent() noexcept {
            return {.passband_frac     = 5.0 / 12.0,
                    .stopband_atten_db = 120.0,
                    .taps_per_branch   = {31, 25, 25, 25, 25},
                    .relaxations       = k_transparent_relaxations};
        }

        /// This profile for a stage whose lower rate is `divisor` times the
        /// chain's lowest rate: the passband fraction over the divisor, the
        /// pins from the relaxation table's row for it (all 0, a search, when
        /// there is none), no further relaxations. relaxed(1) is this profile.
        constexpr profile relaxed(tap::dsp::exact_ratio divisor) const noexcept {
            if (divisor == tap::dsp::exact_ratio{1, 1}) {
                return *this;
            }
            profile r;
            r.passband_frac     = passband_frac * static_cast<double>(divisor.den) / static_cast<double>(divisor.num);
            r.stopband_atten_db = stopband_atten_db;
            r.taps_per_branch   = {0, 0, 0, 0, 0};
            r.relaxations       = {};
            for (const auto& row : relaxations) {
                if (row.divisor == divisor) {
                    r.taps_per_branch = row.taps_per_branch;
                }
            }
            return r;
        }

        /// The passband edge in Hz for a chain whose lowest rate is r_min.
        constexpr double passband_hz(double lowest_rate_hz) const noexcept { return passband_frac * lowest_rate_hz; }

        /// The pinned m for a band of the vocabulary (2, 3, 4, 6, 8), or 0
        /// when the profile carries no pin for it.
        constexpr std::size_t pinned_taps_per_branch(std::size_t band) const noexcept {
            switch (band) {
            case 2:
                return taps_per_branch[0];
            case 3:
                return taps_per_branch[1];
            case 4:
                return taps_per_branch[2];
            case 6:
                return taps_per_branch[3];
            case 8:
                return taps_per_branch[4];
            default:
                return 0;
            }
        }
    };
    // ANCHOR_END: rational_profile

    /// The spec margin every stage design carries: the worst stopband
    /// response on the design grid is at or below -(A + k_margin_db),
    /// bridge's ">= 1 dB margin on a fine grid".
    inline constexpr double k_design_margin_db = 1.0;

    /// The grid the pins were found on and the search uses: 16384 points
    /// from the stopband edge to F_hi / 2. nyquist.h's 1024-point default
    /// steps over a sidelobe of the 8th-band transparent design (-121.7 dB
    /// there, -119.3 dB on 8192 points); 16384 agrees with 65536 within
    /// 0.003 dB for every design of the table (the M2 spike).
    inline constexpr std::size_t k_design_grid_points = 16384;

    /// The search bound for a profile without a pin: N up to 2047 at the
    /// 8th band, four times the longest pin of the table.
    inline constexpr std::size_t k_design_max_taps_per_branch = 128;

    /// Validates a profile: finite, 0 < p < 1/2, A > 0. Throws
    /// std::invalid_argument otherwise (construction time; R9).
    inline void validate(const profile& p) {
        if (!(std::isfinite(p.passband_frac) && std::isfinite(p.stopband_atten_db)) || p.passband_frac <= 0.0
            || p.passband_frac >= 0.5 || p.stopband_atten_db <= 0.0) {
            throw std::invalid_argument("tap::sr::rational::design_stage: bad profile");
        }
    }

    // ANCHOR: rational_design
    /// The taps per branch design_stage<R> will use for a profile: the
    /// profile's pin for the band when it has one, else the search (the
    /// smallest m meeting the stopband with the margin on
    /// k_design_grid_points; 0 when none up to the bound does).
    inline std::size_t taps_per_branch_for(std::size_t band, const profile& p) {
        const std::size_t pinned = p.pinned_taps_per_branch(band);
        if (pinned != 0) {
            return pinned;
        }
        return tap::dsp::search_nyquist_m(band, p.passband_frac, p.stopband_atten_db, k_design_margin_db,
                                          k_design_max_taps_per_branch, k_design_grid_points);
    }

    /// Designs the one Nyquist filter of a stage at ratio R (R2, R4): a
    /// k_band-th-band Kaiser-windowed sinc at the stage's composite rate,
    /// length N = 2 m k_band - 1 with the profile's pinned m for the band
    /// (the table above; a custom profile without a pin is searched), the
    /// centre tap exactly 1, every k_band-th tap from it exactly 0, every
    /// polyphase branch at DC gain 1 (sum L), as nyquist.h's contract
    /// states. The passband fraction is of the stage's lower rate, which is
    /// what the designer takes, because the composite rate is k_band times
    /// it for every ratio of the charter (ratio_traits).
    ///
    /// The up and down designs of one band are the same prototype (a
    /// Nyquist filter is its own transpose up to the gain convention), so
    /// design_stage<ratio<2, 1>> and design_stage<ratio<1, 2>> return the
    /// same vector bit for bit; a mixed ratio's band is its larger factor,
    /// so design_stage<ratio<2, 3>> is the third-band design;
    /// test_design.cpp pins both.
    ///
    /// Construction-time code (runtime double, off the audio path);
    /// allocates (and searches, for an unpinned profile). Throws
    /// std::invalid_argument for a bad profile and std::runtime_error when
    /// no length up to the search bound meets the spec.
    template <rational_ratio R>
    std::vector<double> design_stage(const profile& p) {
        validate(p);
        constexpr std::size_t band = ratio_traits<R>::k_band;
        const std::size_t     m    = taps_per_branch_for(band, p);
        if (m == 0) {
            throw std::runtime_error("tap::sr::rational::design_stage: no length meets the profile's spec");
        }
        std::vector<double> h(tap::dsp::nyquist_length(band, m));
        tap::dsp::design_nyquist(h, band, tap::dsp::kaiser_beta(p.stopband_atten_db));
        return h;
    }
    // ANCHOR_END: rational_design

    /// Taps per phase of a mixed stage's L-phase polyphase table over the
    /// band's design: ceil(N / L), the last phase zero-padded (the MACs per
    /// output of a mixed stage; PLAN.md section 6's M2 table).
    template <rational_ratio R>
    std::size_t stage_taps_per_phase(const profile& p) {
        constexpr std::size_t phases = ratio_traits<R>::k_composite_factor;
        constexpr std::size_t band   = ratio_traits<R>::k_band;
        const std::size_t     n      = tap::dsp::nyquist_length(band, taps_per_branch_for(band, p));
        return (n + phases - 1) / phases;
    }

    /// The group delay of design_stage<R>'s filter in samples at the
    /// composite rate: (N - 1) / 2, an integer (R7, PLAN.md 2.5).
    inline std::size_t stage_group_delay_composite(const std::vector<double>& h) noexcept {
        return h.empty() ? 0 : (h.size() - 1) / 2;
    }

} // namespace tap::sr::rational
