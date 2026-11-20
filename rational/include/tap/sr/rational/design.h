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
#include "tap/dsp/detail/expects.h"
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
    /// The relaxation tables (tools/coverage/matrix.py `pins`, 2026-10-02,
    /// re-pinned 2026-11-20 with the designer's beta for A + 4 dB, X17;
    /// verified against the shipping designer by test_design.cpp): for each
    /// named profile, the pinned m per band at every design divisor a chain
    /// of the coverage matrix uses (PLAN.md 3.1). The 1/1 row is the M2
    /// table; a divisor above 1 relaxes the passband (a stage further up a
    /// chain, exact 2^a 3^b within a family); 147/160 is the one stage that
    /// runs below a 48-family r_min, at bridge's rate, tightened.
    inline constexpr std::array<relaxed_pins, 10> k_super_economy_relaxations = {{
        {{147, 160}, {9, 9, 9, 9, 9}},
        {{1, 1}, {8, 8, 8, 7, 7}},
        {{2, 1}, {4, 4, 4, 5, 5}},
        {{3, 1}, {4, 4, 4, 5, 5}},
        {{4, 1}, {4, 4, 3, 4, 4}},
        {{6, 1}, {4, 3, 3, 4, 4}},
        {{8, 1}, {4, 3, 3, 3, 3}},
        {{9, 1}, {4, 3, 3, 3, 3}},
        {{12, 1}, {3, 3, 3, 3, 3}},
        {{16, 1}, {3, 3, 3, 3, 3}},
    }};
    inline constexpr std::array<relaxed_pins, 10> k_economy_relaxations       = {{
        {{147, 160}, {13, 13, 13, 13, 13}},
        {{1, 1}, {10, 10, 10, 10, 10}},
        {{2, 1}, {5, 4, 4, 5, 5}},
        {{3, 1}, {4, 4, 4, 5, 5}},
        {{4, 1}, {4, 4, 4, 5, 5}},
        {{6, 1}, {4, 3, 3, 4, 4}},
        {{8, 1}, {4, 3, 3, 4, 4}},
        {{9, 1}, {4, 3, 3, 3, 3}},
        {{12, 1}, {4, 3, 3, 3, 3}},
        {{16, 1}, {3, 3, 3, 3, 3}},
    }};
    inline constexpr std::array<relaxed_pins, 10> k_balanced_relaxations      = {{
        {{147, 160}, {17, 17, 17, 17, 17}},
        {{1, 1}, {12, 12, 12, 12, 12}},
        {{2, 1}, {5, 5, 5, 5, 5}},
        {{3, 1}, {4, 4, 4, 5, 5}},
        {{4, 1}, {4, 4, 4, 5, 5}},
        {{6, 1}, {4, 3, 3, 4, 4}},
        {{8, 1}, {4, 3, 3, 4, 4}},
        {{9, 1}, {4, 3, 3, 3, 3}},
        {{12, 1}, {4, 3, 3, 3, 3}},
        {{16, 1}, {3, 3, 3, 3, 3}},
    }};
    inline constexpr std::array<relaxed_pins, 10> k_transparent_relaxations   = {{
        {{147, 160}, {44, 44, 44, 44, 44}},
        {{1, 1}, {25, 25, 25, 25, 25}},
        {{2, 1}, {9, 8, 8, 8, 8}},
        {{3, 1}, {9, 7, 7, 7, 7}},
        {{4, 1}, {9, 7, 7, 7, 7}},
        {{6, 1}, {9, 7, 5, 7, 7}},
        {{8, 1}, {9, 7, 5, 7, 7}},
        {{9, 1}, {8, 7, 5, 7, 7}},
        {{12, 1}, {6, 7, 5, 7, 7}},
        {{16, 1}, {6, 7, 5, 7, 7}},
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
    /// M2 design spike, notebooks/design_spike.ipynb, 2026-10-02, re-pinned
    /// 2026-11-20 by tools/coverage/matrix.py under X17's rule; verified by
    /// test_design.cpp against the shipping designer): the minimal m whose
    /// L-th-band design, its Kaiser beta taken for A + 4 dB
    /// (k_design_beta_offset_db), meets the stopband A with >= 1 dB margin on
    /// a 16384-point grid (k_design_grid_points), N = 2 m B - 1:
    ///
    /// | profile       | A      | p     | m: B=2 | B=3 | B=4 | B=6 | B=8 | N: B=2 | B=3 | B=4 | B=6 | B=8 |
    /// |---------------|--------|-------|--------|-----|-----|-----|-----|--------|-----|-----|-----|-----|
    /// | super_economy |  70 dB | 1/3   | 8      | 8   | 8   | 7   | 7   | 31     | 47  | 63  | 83  | 111 |
    /// | economy       |  70 dB | 3/8   | 10     | 10  | 10  | 10  | 10  | 39     | 59  | 79  | 119 | 159 |
    /// | balanced      |  70 dB | 19/48 | 12     | 12  | 12  | 12  | 12  | 47     | 71  | 95  | 143 | 191 |
    /// | transparent   | 120 dB | 5/12  | 25     | 25  | 25  | 25  | 25  | 99     | 149 | 199 | 299 | 399 |
    ///
    /// Measured worst stopband -71.9 .. -74.4 dB at the 70 dB tiers and
    /// -122.2 .. -123.1 dB at transparent, passband ripple <= 0.0024 dB and
    /// <= 0.00001 dB (PLAN.md 2.3's candidates are 0.01 / 0.0001). A custom
    /// profile leaves its pins at 0 and design_stage searches instead, by
    /// the same rule.
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
        std::array<std::size_t, 5> taps_per_branch = {10, 10, 10, 10, 10};
        /// The pins at the other design divisors (relaxed()); empty for a
        /// custom profile.
        std::span<const relaxed_pins> relaxations = k_economy_relaxations;

        /// The speed-first default: 70 dB, 3/8 of the lowest rate (18 kHz at 48).
        static constexpr profile economy() noexcept { return {}; }
        /// Voice/comms tier: 70 dB, 1/3 (16 kHz at 48). Never a default.
        static constexpr profile super_economy() noexcept {
            return {.passband_frac     = 1.0 / 3.0,
                    .stopband_atten_db = 70.0,
                    .taps_per_branch   = {8, 8, 8, 7, 7},
                    .relaxations       = k_super_economy_relaxations};
        }
        /// 70 dB, flat to 19/48 (19 kHz at 48).
        static constexpr profile balanced() noexcept {
            return {.passband_frac     = 19.0 / 48.0,
                    .stopband_atten_db = 70.0,
                    .taps_per_branch   = {12, 12, 12, 12, 12},
                    .relaxations       = k_balanced_relaxations};
        }
        /// Pristine tier: 120 dB, 5/12 (20 kHz at 48).
        static constexpr profile transparent() noexcept {
            return {.passband_frac     = 5.0 / 12.0,
                    .stopband_atten_db = 120.0,
                    .taps_per_branch   = {25, 25, 25, 25, 25},
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

    /// The offset of the designer's Kaiser beta (X17, docs/X4_X8_PLAN.md):
    /// every stage is designed with kaiser_beta(A + 4 dB) and accepted at
    /// -(A + k_design_margin_db). Kaiser's beta for A alone puts a short
    /// design's sidelobes at -68 to -70.6 dB, inside the margin, so the
    /// search ran on to a longer design (a relaxed 70 dB half-band stayed at
    /// m = 7 at divisors 2 to 4); a beta for A + 4 dB reaches the margin
    /// several taps sooner: 127 of the 200 relaxation pins moved, the mean
    /// economy cost of the coverage matrix's rows fell 6.3 %, and every Q15
    /// stage attains the 70 dB tier at its own rate and at every design
    /// divisor (A + 3 dB, the other offset measured, misses it; PLAN.md
    /// section 6, "The designer's beta, X17").
    inline constexpr double k_design_beta_offset_db = 4.0;

    /// The Kaiser beta of every stage design for a profile: Kaiser's
    /// published fit (tap::dsp::kaiser_beta) for A + k_design_beta_offset_db.
    inline double design_beta(const profile& p) noexcept {
        return tap::dsp::kaiser_beta(p.stopband_atten_db + k_design_beta_offset_db);
    }

    /// The grid the pins were found on and the search uses: 16384 points
    /// from the stopband edge to F_hi / 2. nyquist.h's 1024-point default
    /// steps over a sidelobe of the 8th-band transparent design (-121.7 dB
    /// there, -119.3 dB on 8192 points); 16384 agrees with 65536 within
    /// 0.003 dB for every design of the table (the M2 spike).
    inline constexpr std::size_t k_design_grid_points = 16384;

    /// The search bound for a profile without a pin: 128 taps per branch, N
    /// up to 2047 at the 8th band, 2.9 times the longest pin of the tables
    /// (44, transparent at the 147/160 divisor). A feasible spec above it
    /// is reported as unmeetable, by design: a stage that long has no place
    /// in a speed-first engine.
    inline constexpr std::size_t k_design_max_taps_per_branch = 128;

    /// The reason a profile is rejected, or nullptr when it designs: finite,
    /// 0 < p < 1/2, A > 0 (R9). noexcept and allocation-free, the family's
    /// error model; design_stage raises the reason as std::invalid_argument,
    /// and basic_stage::validate composes it. A relaxed profile's passband is
    /// the base's times the divisor's reciprocal (profile::relaxed), so a
    /// custom base passband at or above 1/2 of the divisor's reciprocal fails
    /// here with the relaxed number.
    inline const char* validate(const profile& p) noexcept {
        if (!(std::isfinite(p.passband_frac) && std::isfinite(p.stopband_atten_db)) || p.passband_frac <= 0.0
            || p.passband_frac >= 0.5 || p.stopband_atten_db <= 0.0) {
            return "tap::sr::rational::design_stage: bad profile (passband_frac must be in (0, 1/2) of the stage's "
                   "lower rate, after any divisor; stopband_atten_db > 0)";
        }
        return nullptr;
    }

    /// The text design_stage and stage_taps_per_phase raise when no length up
    /// to the search bound meets the spec; basic_stage::validate returns it.
    inline constexpr const char* k_unmeetable_spec_reason =
        "tap::sr::rational::design_stage: no length up to the search bound (128 taps per branch, "
        "k_design_max_taps_per_branch) meets the profile's spec";

    // ANCHOR: rational_design
    /// The taps per branch design_stage<R> will use for a profile: the
    /// profile's pin for the band when it has one, else the search (the
    /// smallest m whose design with design_beta(p) meets the stopband with
    /// the margin on k_design_grid_points; 0 when none up to the bound does).
    ///
    /// The search is DspTap's search_nyquist_m, which designs with
    /// kaiser_beta of the attenuation it is given and accepts at that
    /// attenuation plus the margin. Given A + x and k_design_margin_db - x
    /// (x = k_design_beta_offset_db) it designs with beta for A + x and
    /// accepts at -(A + k_design_margin_db), this engine's criterion: the
    /// same m as a search with the beta as its own argument at every key the
    /// coverage matrix designs (spike X-0, 300 of 300 keys), on DspTap 0.2.0.
    inline std::size_t taps_per_branch_for(std::size_t band, const profile& p) {
        const std::size_t pinned = p.pinned_taps_per_branch(band);
        if (pinned != 0) {
            return pinned;
        }
        return tap::dsp::search_nyquist_m(band, p.passband_frac, p.stopband_atten_db + k_design_beta_offset_db,
                                          k_design_margin_db - k_design_beta_offset_db, k_design_max_taps_per_branch,
                                          k_design_grid_points);
    }

    /// Designs the one Nyquist filter of a stage at ratio R (R2, R4): a
    /// k_band-th-band Kaiser-windowed sinc at the stage's composite rate,
    /// length N = 2 m k_band - 1 with the profile's pinned m for the band
    /// (the table above; a custom profile without a pin is searched) and the
    /// window's beta for A + 4 dB (design_beta), the centre tap exactly 1,
    /// every k_band-th tap from it exactly 0, every polyphase branch at DC
    /// gain 1 (sum L), as nyquist.h's contract states. The passband
    /// fraction is of the stage's lower rate, which is what the designer
    /// takes, because the composite rate is k_band times it for every ratio
    /// of the charter (ratio_traits).
    ///
    /// The up and down designs of one band are the same prototype (a
    /// Nyquist filter is its own transpose up to the gain convention), so
    /// design_stage<ratio<2, 1>> and design_stage<ratio<1, 2>> return the
    /// same vector bit for bit; a mixed ratio's band is its larger factor,
    /// so design_stage<ratio<2, 3>> is the third-band design;
    /// test_design.cpp pins both.
    ///
    /// Construction-time code (runtime double, off the audio path);
    /// allocates (and searches, for an unpinned profile). Rejects through
    /// tap::dsp::raise: std::invalid_argument for a bad profile (validate's
    /// text) and std::runtime_error when no length up to the search bound
    /// meets the spec (k_unmeetable_spec_reason); a build without exceptions
    /// terminates there, so it asks basic_stage::validate first.
    template <rational_ratio R>
    std::vector<double> design_stage(const profile& p) {
        if (const char* why = validate(p)) {
            tap::dsp::raise<std::invalid_argument>(why);
        }
        constexpr std::size_t band = ratio_traits<R>::k_band;
        const std::size_t     m    = taps_per_branch_for(band, p);
        if (m == 0) {
            tap::dsp::raise<std::runtime_error>(k_unmeetable_spec_reason);
        }
        std::vector<double> h(tap::dsp::nyquist_length(band, m));
        tap::dsp::design_nyquist(h, band, design_beta(p));
        return h;
    }
    // ANCHOR_END: rational_design

    /// Taps per phase of a mixed stage's L-phase polyphase table over the
    /// band's design: ceil(N / L), the last phase zero-padded (the MACs per
    /// output of a mixed stage; PLAN.md section 6's M2 table). Validates and
    /// searches as design_stage does, and throws the same way.
    template <rational_ratio R>
    std::size_t stage_taps_per_phase(const profile& p) {
        if (const char* why = validate(p)) {
            tap::dsp::raise<std::invalid_argument>(why);
        }
        constexpr std::size_t phases = ratio_traits<R>::k_composite_factor;
        constexpr std::size_t band   = ratio_traits<R>::k_band;
        const std::size_t     m      = taps_per_branch_for(band, p);
        if (m == 0) {
            tap::dsp::raise<std::runtime_error>(k_unmeetable_spec_reason);
        }
        const std::size_t n = tap::dsp::nyquist_length(band, m);
        return (n + phases - 1) / phases;
    }

    /// The group delay of design_stage<R>'s filter in samples at the
    /// composite rate: (N - 1) / 2, an integer (R7, PLAN.md 2.5).
    inline std::size_t stage_group_delay_composite(const std::vector<double>& h) noexcept {
        return h.empty() ? 0 : (h.size() - 1) / 2;
    }

} // namespace tap::sr::rational
