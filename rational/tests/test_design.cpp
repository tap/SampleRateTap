// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// M1 battery for design.h: the profiles' numbers (R5), the stage design's
// Nyquist structure through this engine's build (exact centre and zeros,
// per-branch unity, the symmetric transition), the measured lengths of
// PLAN.md 2.3 at every profile for the half and third bands (the same
// numbers DspTap's test_nyquist.cpp pins three of), the up/down transpose
// identity the plan's M2 table relies on, the spec with its margin at every
// band of the vocabulary, and BadProfilesThrow.

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "tap/dsp/nyquist.h"
#include "tap/sr/rational/rational.h"
#include "tap_sr_test/expect_rejected.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)

    // design_stage by band, for the table loop (the band is what the
    // design depends on; every ratio of the band shares it).
    std::vector<double> design_stage_for_band(std::size_t band, const profile& p) {
        switch (band) {
        case 2:
            return design_stage<up2>(p);
        case 3:
            return design_stage<up3>(p);
        case 4:
            return design_stage<ratio<4, 1>>(p);
        case 6:
            return design_stage<up6>(p);
        default:
            return design_stage<up8>(p);
        }
    }

    TEST(Design, ProfilesAreBridgesFourEdgesAsFractions) {
        EXPECT_DOUBLE_EQ(profile::super_economy().passband_hz(48000.0), 16000.0);
        EXPECT_DOUBLE_EQ(profile::economy().passband_hz(48000.0), 18000.0);
        EXPECT_DOUBLE_EQ(profile::balanced().passband_hz(48000.0), 19000.0);
        EXPECT_DOUBLE_EQ(profile::transparent().passband_hz(48000.0), 20000.0);
        EXPECT_DOUBLE_EQ(profile::economy().passband_hz(44100.0), 16537.5); // the edge scales with the rate
        EXPECT_DOUBLE_EQ(profile::super_economy().stopband_atten_db, 70.0);
        EXPECT_DOUBLE_EQ(profile::economy().stopband_atten_db, 70.0);
        EXPECT_DOUBLE_EQ(profile::balanced().stopband_atten_db, 70.0);
        EXPECT_DOUBLE_EQ(profile::transparent().stopband_atten_db, 120.0);
        static_assert(profile{}.passband_frac == profile::economy().passband_frac, "economy is the default");
    }

    // The M2 table (PLAN.md section 6; design.h's profile docstring):
    // measured 2026-10-02 by notebooks/design_spike.ipynb (the numpy leg),
    // re-pinned 2026-11-20 under X17's rule by tools/coverage/matrix.py (the
    // numpy leg since) and re-measured here through the shipping designer:
    // for every band of the vocabulary and every profile, the pinned m is
    // the smallest whose design, its Kaiser beta taken for A + 4 dB
    // (design_beta), meets the stopband with the 1 dB margin on the
    // 16384-point grid (m - 1 misses it), and the pin equals what the
    // search finds.
    struct pinned_row {
        std::size_t band;
        profile     p;
        std::size_t m;
        std::size_t n;
        double      worst_db; // measured, 16384-point grid
    };

    const pinned_row k_m2_table[] = {
        {.band = 2, .p = profile::super_economy(), .m = 8, .n = 31, .worst_db = -72.44},
        {.band = 2, .p = profile::economy(), .m = 10, .n = 39, .worst_db = -73.05},
        {.band = 2, .p = profile::balanced(), .m = 12, .n = 47, .worst_db = -72.49},
        {.band = 2, .p = profile::transparent(), .m = 25, .n = 99, .worst_db = -122.16},
        {.band = 3, .p = profile::super_economy(), .m = 8, .n = 47, .worst_db = -73.63},
        {.band = 3, .p = profile::economy(), .m = 10, .n = 59, .worst_db = -72.47},
        {.band = 3, .p = profile::balanced(), .m = 12, .n = 71, .worst_db = -72.36},
        {.band = 3, .p = profile::transparent(), .m = 25, .n = 149, .worst_db = -122.83},
        {.band = 4, .p = profile::super_economy(), .m = 8, .n = 63, .worst_db = -74.35},
        {.band = 4, .p = profile::economy(), .m = 10, .n = 79, .worst_db = -72.51},
        {.band = 4, .p = profile::balanced(), .m = 12, .n = 95, .worst_db = -72.46},
        {.band = 4, .p = profile::transparent(), .m = 25, .n = 199, .worst_db = -122.93},
        {.band = 6, .p = profile::super_economy(), .m = 7, .n = 83, .worst_db = -71.86},
        {.band = 6, .p = profile::economy(), .m = 10, .n = 119, .worst_db = -72.60},
        {.band = 6, .p = profile::balanced(), .m = 12, .n = 143, .worst_db = -72.59},
        {.band = 6, .p = profile::transparent(), .m = 25, .n = 299, .worst_db = -123.01},
        {.band = 8, .p = profile::super_economy(), .m = 7, .n = 111, .worst_db = -72.13},
        {.band = 8, .p = profile::economy(), .m = 10, .n = 159, .worst_db = -72.66},
        {.band = 8, .p = profile::balanced(), .m = 12, .n = 191, .worst_db = -72.67},
        {.band = 8, .p = profile::transparent(), .m = 25, .n = 399, .worst_db = -123.06},
    };

    // The ripple candidates of PLAN.md 2.3, now bounds: per 70 dB stage
    // 0.01 dB, per transparent stage 0.0001 dB (measured <= 0.0024 and
    // <= 0.00001 since X17; <= 0.0025 and <= 0.00001 with beta for A).
    double ripple_bound_db(const profile& p) {
        return p.stopband_atten_db >= 100.0 ? 0.0001 : 0.01;
    }

    double passband_ripple_db(const std::vector<double>& h, std::size_t band, double p) {
        double       worst = 0.0;
        const double edge  = p / static_cast<double>(band);
        for (int g = 0; g <= 2000; ++g) {
            worst = std::max(worst, std::fabs(tap::dsp::nyquist_response_db(h, band, edge * g / 2000.0)));
        }
        return worst;
    }

    TEST(Design, PinnedLengthsAreTheM2Table) {
        for (const auto& row : k_m2_table) {
            EXPECT_EQ(row.p.pinned_taps_per_branch(row.band), row.m) << "band " << row.band;
            EXPECT_EQ(taps_per_branch_for(row.band, row.p), row.m) << "band " << row.band;
            EXPECT_EQ(tap::dsp::nyquist_length(row.band, row.m), row.n) << "band " << row.band;
        }
        EXPECT_EQ(design_stage<up2>(profile::economy()).size(), 39u);
        EXPECT_EQ(design_stage<up3>(profile::transparent()).size(), 149u);
        EXPECT_EQ(design_stage<down8>(profile::transparent()).size(), 399u);
        // The nonzero counts the plan states beside the lengths: 2m(L - 1) + 1.
        EXPECT_EQ(tap::dsp::nyquist_nonzero_taps(2, 10), 21u);
        EXPECT_EQ(tap::dsp::nyquist_nonzero_taps(3, 10), 41u);
        EXPECT_EQ(tap::dsp::nyquist_nonzero_taps(8, 25), 351u);
    }

    // Every pin meets the spec with the margin on the design grid, m - 1
    // misses it, the measured worst stopband is the table's within 0.05 dB,
    // and the ripple is within 2.3's candidate. The 70 dB rows run on every
    // leg; the transparent rows are the host's (bare_metal_main.cpp).
    void expect_pin_is_minimal_and_measured(const pinned_row& row) {
        const auto h = design_stage_for_band(row.band, row.p);
        ASSERT_EQ(h.size(), row.n) << "band " << row.band;
        const double worst =
            tap::dsp::nyquist_worst_stopband_db(h, row.band, row.p.passband_frac, k_design_grid_points);
        EXPECT_LE(worst, -(row.p.stopband_atten_db + k_design_margin_db)) << "band " << row.band;
        EXPECT_NEAR(worst, row.worst_db, 0.05) << "band " << row.band;
        std::vector<double> g(tap::dsp::nyquist_length(row.band, row.m - 1));
        tap::dsp::design_nyquist(g, row.band, design_beta(row.p));
        EXPECT_GT(tap::dsp::nyquist_worst_stopband_db(g, row.band, row.p.passband_frac, k_design_grid_points),
                  -(row.p.stopband_atten_db + k_design_margin_db))
            << "band " << row.band << ": m - 1 meets the spec, the pin is not minimal";
        EXPECT_LE(passband_ripple_db(h, row.band, row.p.passband_frac), ripple_bound_db(row.p)) << "band " << row.band;
    }

    TEST(Design, PinsAreMinimalAndMeasuredAt70dB) {
        for (const auto& row : k_m2_table) {
            if (row.p.stopband_atten_db < 100.0) {
                expect_pin_is_minimal_and_measured(row);
            }
        }
    }

    TEST(Design, PinsAreMinimalAndMeasuredAtTransparent) {
        for (const auto& row : k_m2_table) {
            if (row.p.stopband_atten_db >= 100.0) {
                expect_pin_is_minimal_and_measured(row);
            }
        }
    }

    // The two rows a 1024-point search got wrong (the finding behind
    // tap/DspTap#50): band 4 balanced and band 8 transparent, where the
    // coarse grid steps over a sidelobe of a design with beta for A and
    // accepts one branch tap fewer. DspTap's search still does (these calls
    // design with beta for A). Under the rule the pins use since X17 (beta
    // for A + 4 dB) the 1024-point grid finds the same m as the design grid
    // at every entry of the tables (measured 2026-11-20), so the grid stays
    // part of the criterion, not a correction a current pin needs.
    TEST(Design, TheCoarseGridWouldUnderPinTwoRows) {
        EXPECT_EQ(tap::dsp::search_nyquist_m(4, 19.0 / 48.0, 70.0), 13u);
        EXPECT_EQ(tap::dsp::search_nyquist_m(4, 19.0 / 48.0, 70.0, 1.0, 256, k_design_grid_points), 14u);
        EXPECT_EQ(tap::dsp::search_nyquist_m(8, 5.0 / 12.0, 120.0), 24u);
        EXPECT_EQ(tap::dsp::search_nyquist_m(8, 5.0 / 12.0, 120.0, 1.0, 256, k_design_grid_points), 25u);
    }

    // The mixed stages' taps per phase, ceil(N / L) over the band's design
    // (the M2 table's last row): the MACs per output of a mixed stage.
    TEST(Design, MixedStagesTapsPerPhaseAreTheM2Table) {
        const profile eco = profile::economy(), tr = profile::transparent();
        EXPECT_EQ(stage_taps_per_phase<ratio3over2>(eco), 20u); // N = 59 over 3 phases
        EXPECT_EQ(stage_taps_per_phase<ratio2over3>(eco), 30u); // N = 59 over 2
        EXPECT_EQ(stage_taps_per_phase<ratio4over3>(eco), 20u); // N = 79 over 4
        EXPECT_EQ(stage_taps_per_phase<ratio3over4>(eco), 27u); // N = 79 over 3
        EXPECT_EQ(stage_taps_per_phase<ratio8over3>(eco), 20u); // N = 159 over 8
        EXPECT_EQ(stage_taps_per_phase<ratio3over8>(eco), 53u); // N = 159 over 3
        EXPECT_EQ(stage_taps_per_phase<ratio3over2>(tr), 50u);
        EXPECT_EQ(stage_taps_per_phase<ratio2over3>(tr), 75u);
        EXPECT_EQ(stage_taps_per_phase<ratio4over3>(tr), 50u);
        EXPECT_EQ(stage_taps_per_phase<ratio3over4>(tr), 67u);
        EXPECT_EQ(stage_taps_per_phase<ratio8over3>(tr), 50u);
        EXPECT_EQ(stage_taps_per_phase<ratio3over8>(tr), 133u);
        EXPECT_EQ(stage_taps_per_phase<ratio3over2>(profile::super_economy()), 16u);
        EXPECT_EQ(stage_taps_per_phase<ratio3over8>(profile::balanced()), 64u);
    }

    // A custom profile without pins is searched on the same grid, so it
    // lands where the table would.
    TEST(Design, UnpinnedProfileIsSearchedOnTheDesignGrid) {
        profile custom         = profile::balanced();
        custom.taps_per_branch = {0, 0, 0, 0, 0};
        EXPECT_EQ(taps_per_branch_for(4, custom), 12u);
        EXPECT_TRUE(design_stage<ratio4over3>(custom) == design_stage<ratio4over3>(profile::balanced()));
        profile unknown_band = profile::economy();
        EXPECT_EQ(unknown_band.pinned_taps_per_branch(5), 0u); // not a band of the vocabulary
    }

    TEST(Design, UpAndDownOfOneBandAreTheSamePrototype) {
        // The plan's M2 table relies on it: one pin serves both directions.
        EXPECT_TRUE(design_stage<up2>(profile::economy()) == design_stage<down2>(profile::economy()));
        EXPECT_TRUE(design_stage<up3>(profile::transparent()) == design_stage<down3>(profile::transparent()));
        EXPECT_TRUE(design_stage<up6>(profile::economy()) == design_stage<down6>(profile::economy()));
        // And a mixed ratio's band is its larger factor (R2): 2/3 and 3/2
        // are third-band designs at the same lower-rate fraction, so they
        // are the third-band design.
        EXPECT_TRUE(design_stage<ratio2over3>(profile::economy()) == design_stage<up3>(profile::economy()));
        EXPECT_TRUE(design_stage<ratio3over2>(profile::economy()) == design_stage<up3>(profile::economy()));
        EXPECT_TRUE(design_stage<ratio3over8>(profile::economy()) == design_stage<up8>(profile::economy()));
    }

    template <rational_ratio R>
    void expect_nyquist_structure(const profile& p) {
        constexpr std::size_t band = ratio_traits<R>::k_band;
        const auto            h    = design_stage<R>(p);
        ASSERT_TRUE(tap::dsp::is_nyquist_length(band, h.size()));
        const std::size_t c = (h.size() - 1) / 2;
        EXPECT_EQ(h[c], 1.0);
        EXPECT_EQ(stage_group_delay_composite(h), c);
        for (std::size_t k = 1; c >= k * band; ++k) {
            EXPECT_EQ(h[c - k * band], 0.0) << k;
            EXPECT_EQ(h[c + k * band], 0.0) << k;
        }
        for (std::size_t j = 0; j < band; ++j) {
            double sum = 0.0;
            for (std::size_t i = j; i < h.size(); i += band) {
                sum += h[i];
            }
            EXPECT_NEAR(sum, 1.0, 1e-15) << "branch " << j;
        }
        // The spec, with the margin, at the stage's lower-rate passband
        // fraction, on the design grid.
        const double worst = tap::dsp::nyquist_worst_stopband_db(h, band, p.passband_frac, k_design_grid_points);
        EXPECT_LE(worst, -(p.stopband_atten_db + k_design_margin_db));
        // Flat to the edge (f_pass / composite = p / band).
        EXPECT_LE(passband_ripple_db(h, band, p.passband_frac), ripple_bound_db(p));
    }

    TEST(Design, EveryBandOfTheVocabularyMeetsTheSpecWithTheMargin) {
        expect_nyquist_structure<up2>(profile::economy());
        expect_nyquist_structure<down3>(profile::economy());
        expect_nyquist_structure<up6>(profile::economy());
        expect_nyquist_structure<down8>(profile::economy());
        expect_nyquist_structure<ratio4over3>(profile::economy()); // band 4: the mixed ratio's design
        expect_nyquist_structure<down2>(profile::super_economy());
        expect_nyquist_structure<up3>(profile::balanced());
    }

    TEST(Design, TransparentMeetsTheSpecWithTheMargin) {
        expect_nyquist_structure<up2>(profile::transparent());
        expect_nyquist_structure<down3>(profile::transparent());
    }

    TEST(Design, BadProfilesThrow) {
        profile p       = profile::economy();
        p.passband_frac = 0.5; // the lower rate's Nyquist: no transition band
        TAP_SR_EXPECT_REJECTED((design_stage<up2>(p)), std::invalid_argument);
        p.passband_frac = 0.0;
        TAP_SR_EXPECT_REJECTED((design_stage<up2>(p)), std::invalid_argument);
        p.passband_frac = -0.1;
        TAP_SR_EXPECT_REJECTED((design_stage<up2>(p)), std::invalid_argument);
        profile q           = profile::economy();
        q.stopband_atten_db = 0.0;
        TAP_SR_EXPECT_REJECTED((design_stage<up2>(q)), std::invalid_argument);
        q.stopband_atten_db = std::numeric_limits<double>::quiet_NaN();
        TAP_SR_EXPECT_REJECTED((design_stage<up2>(q)), std::invalid_argument);
        // And the good ones do not.
        TAP_SR_EXPECT_ACCEPTED((design_stage<up2>(profile::economy())));
        TAP_SR_EXPECT_ACCEPTED((design_stage<up2>(profile::transparent())));
        // validate() is the same verdict as a noexcept reason (A3): the text the
        // throw carries, or nullptr.
        static_assert(noexcept(validate(p)));
        EXPECT_NE(validate(p), nullptr);
        EXPECT_NE(validate(q), nullptr);
        EXPECT_EQ(validate(profile::economy()), nullptr);
        TAP_SR_EXPECT_REJECTED_WITH((design_stage<up2>(q)), std::invalid_argument, validate(q));
    }

    // A spec no length within the search bound meets (no pin: searched to
    // k_design_max_taps_per_branch on the design grid; seconds on a host,
    // excluded from the emulated legs by bare_metal_main.cpp and ci.yml).
    // The relaxation tables (M4, design.h; tools/coverage/matrix.py `pins`):
    // every entry of every named profile is what the shipping search finds
    // for the band at the relaxed passband, on the design grid with the
    // margin and beta for A + 4 dB — the same criterion as the M2 pins
    // (DspTap's search given A + x and the margin less x designs with beta
    // for A + x and accepts at the margin, design.h's
    // taps_per_branch_for). The 1/1 row is the M2
    // table; 147/160 tightens (the stage at bridge's rate under a 48-family
    // r_min); the others relax. The 70 dB tables run on every leg, the
    // transparent one (designs up to N = 703) on the hosts.
    void expect_relaxations_are_the_search(const profile& p) {
        for (const auto& row : p.relaxations) {
            const profile r = p.relaxed(row.divisor);
            EXPECT_EQ(r.taps_per_branch, row.taps_per_branch);
            if (row.divisor == tap::dsp::exact_ratio{1, 1}) {
                EXPECT_EQ(row.taps_per_branch, p.taps_per_branch);
            }
            const std::size_t bands[] = {2, 3, 4, 6, 8};
            for (std::size_t b = 0; b < 5; ++b) {
                const std::size_t found = tap::dsp::search_nyquist_m(
                    bands[b], r.passband_frac, r.stopband_atten_db + k_design_beta_offset_db,
                    k_design_margin_db - k_design_beta_offset_db, k_design_max_taps_per_branch, k_design_grid_points);
                EXPECT_EQ(found, row.taps_per_branch[b])
                    << "divisor " << row.divisor.num << "/" << row.divisor.den << " band " << bands[b];
            }
        }
    }

    TEST(Design, RelaxationTablesAreTheSearchAt70dB) {
        expect_relaxations_are_the_search(profile::super_economy());
        expect_relaxations_are_the_search(profile::economy());
        expect_relaxations_are_the_search(profile::balanced());
        EXPECT_EQ(profile::economy().relaxations.size(), 10u);
    }

    TEST(Design, RelaxationTablesAreTheSearchAtTransparent) {
        expect_relaxations_are_the_search(profile::transparent());
    }

    TEST(Design, UnmeetableSpecThrows) {
        const profile r = {.passband_frac = 0.4999, .stopband_atten_db = 160.0, .taps_per_branch = {0, 0, 0, 0, 0}};
        // One search, not two: the text is the throw's (the designer's reason).
        TAP_SR_EXPECT_REJECTED_WITH((design_stage<up2>(r)), std::runtime_error, k_unmeetable_spec_reason);
    }

} // namespace
