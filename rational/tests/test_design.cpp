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

#include <cstddef>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "tap/dsp/nyquist.h"
#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)

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

    // PLAN.md 2.3's measured lengths (v0.3, 2026-10-01, by search_nyquist_m
    // on the shipped designer): N / nonzero taps per profile at the half
    // and third bands. Re-measured here through this engine's design_stage.
    struct expected_length {
        profile     p;
        std::size_t half_band_n;
        std::size_t third_band_n;
    };

    TEST(Design, LengthsMatchThePlansMeasuredCounts) {
        const expected_length table[] = {
            {.p = profile::super_economy(), .half_band_n = 35, .third_band_n = 47},
            {.p = profile::economy(), .half_band_n = 43, .third_band_n = 65},
            {.p = profile::balanced(), .half_band_n = 51, .third_band_n = 77},
            {.p = profile::transparent(), .half_band_n = 123, .third_band_n = 149},
        };
        for (const auto& row : table) {
            EXPECT_EQ(design_stage<up_2>(row.p).size(), row.half_band_n) << row.p.passband_frac;
            EXPECT_EQ(design_stage<up_3>(row.p).size(), row.third_band_n) << row.p.passband_frac;
        }
        // The nonzero counts the plan states beside them: 2m(L - 1) + 1.
        EXPECT_EQ(tap::dsp::nyquist_nonzero_taps(2, (43 + 1) / 4), 23u);
        EXPECT_EQ(tap::dsp::nyquist_nonzero_taps(3, (65 + 1) / 6), 45u);
    }

    TEST(Design, UpAndDownOfOneBandAreTheSamePrototype) {
        // The plan's M2 table relies on it: one pin serves both directions.
        EXPECT_TRUE(design_stage<up_2>(profile::economy()) == design_stage<down_2>(profile::economy()));
        EXPECT_TRUE(design_stage<up_3>(profile::transparent()) == design_stage<down_3>(profile::transparent()));
        EXPECT_TRUE(design_stage<up_6>(profile::economy()) == design_stage<down_6>(profile::economy()));
        // And a mixed ratio's band is its larger factor (R2): 2/3 and 3/2
        // are third-band designs at the same lower-rate fraction, so they
        // are the third-band design.
        EXPECT_TRUE(design_stage<ratio_2_3>(profile::economy()) == design_stage<up_3>(profile::economy()));
        EXPECT_TRUE(design_stage<ratio_3_2>(profile::economy()) == design_stage<up_3>(profile::economy()));
        EXPECT_TRUE(design_stage<ratio_3_8>(profile::economy()) == design_stage<up_8>(profile::economy()));
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
        // The spec, with the margin, at the stage's lower-rate passband fraction.
        const double worst = tap::dsp::nyquist_worst_stopband_db(h, band, p.passband_frac);
        EXPECT_LE(worst, -(p.stopband_atten_db + k_design_margin_db));
        // Minimality: one branch tap fewer misses it.
        const std::size_t   m = (h.size() + 1) / (2 * band);
        std::vector<double> g(tap::dsp::nyquist_length(band, m - 1));
        tap::dsp::design_nyquist(g, band, tap::dsp::kaiser_beta(p.stopband_atten_db));
        EXPECT_GT(tap::dsp::nyquist_worst_stopband_db(g, band, p.passband_frac),
                  -(p.stopband_atten_db + k_design_margin_db));
        // Flat to the edge (f_pass / composite = p / band).
        for (double f = 0.0; f <= p.passband_frac / static_cast<double>(band); f += 0.01 / static_cast<double>(band)) {
            EXPECT_NEAR(tap::dsp::nyquist_response_db(h, band, f), 0.0, 0.02) << f;
        }
    }

    TEST(Design, EveryBandOfTheVocabularyMeetsTheSpecWithTheMargin) {
        expect_nyquist_structure<up_2>(profile::economy());
        expect_nyquist_structure<down_3>(profile::economy());
        expect_nyquist_structure<up_6>(profile::economy());
        expect_nyquist_structure<down_8>(profile::economy());
        expect_nyquist_structure<ratio_4_3>(profile::economy()); // band 4: the mixed ratio's design
        expect_nyquist_structure<down_2>(profile::super_economy());
        expect_nyquist_structure<up_3>(profile::balanced());
    }

    TEST(Design, TransparentMeetsTheSpecWithTheMargin) {
        expect_nyquist_structure<up_2>(profile::transparent());
        expect_nyquist_structure<down_3>(profile::transparent());
    }

    TEST(Design, BadProfilesThrow) {
        profile p       = profile::economy();
        p.passband_frac = 0.5; // the lower rate's Nyquist: no transition band
        EXPECT_THROW((design_stage<up_2>(p)), std::invalid_argument);
        p.passband_frac = 0.0;
        EXPECT_THROW((design_stage<up_2>(p)), std::invalid_argument);
        p.passband_frac = -0.1;
        EXPECT_THROW((design_stage<up_2>(p)), std::invalid_argument);
        profile q           = profile::economy();
        q.stopband_atten_db = 0.0;
        EXPECT_THROW((design_stage<up_2>(q)), std::invalid_argument);
        q.stopband_atten_db = std::numeric_limits<double>::quiet_NaN();
        EXPECT_THROW((design_stage<up_2>(q)), std::invalid_argument);
        // A spec no length within the search bound meets.
        profile r = {.passband_frac = 0.4999, .stopband_atten_db = 160.0};
        EXPECT_THROW((design_stage<up_2>(r)), std::runtime_error);
        // And the good ones do not.
        EXPECT_NO_THROW((design_stage<up_2>(profile::economy())));
        EXPECT_NO_THROW((design_stage<up_2>(profile::transparent())));
    }

} // namespace
