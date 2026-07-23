// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
//
// Contract battery for the prototype designs: every direction x profile
// meets its rated stopband with >= 1 dB margin and holds the passband flat,
// measured by direct DFT on the double-precision prototype. The pinned
// taps-per-phase in profile{} came from the M2 design spike
// (notebooks/design_spike.ipynb); this battery is what keeps them honest in
// CI. Measured numbers are printed [ measured ] for the record.

#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "tap/ratio/design.h"

namespace {

    using tap::ratio::design_prototype;
    using tap::ratio::direction;
    using tap::ratio::profile;
    using tap::ratio::ratio_traits;

    // Direct DFT magnitude in dB, normalized so the passband sits at 0 dB.
    // f is in Hz at the direction's input rate; the prototype rate is L * fs.
    double response_db(const std::vector<double>& h, std::size_t num_phases, double fs, double f) {
        const double         proto_rate = static_cast<double>(num_phases) * fs;
        std::complex<double> acc{0.0, 0.0};
        for (std::size_t m = 0; m < h.size(); ++m) {
            const double ang = -2.0 * std::numbers::pi * f * static_cast<double>(m) / proto_rate;
            acc += h[m] * std::polar(1.0, ang);
        }
        return 20.0 * std::log10(std::abs(acc) / static_cast<double>(num_phases));
    }

    template <direction D>
    void check_meets_spec(const profile& p, const char* name) {
        using traits = ratio_traits<D>;
        const std::vector<double> h = design_prototype<D>(p);
        ASSERT_EQ(h.size(), traits::k_phases * p.taps<D>());

        // Passband: flat within +/-0.01 dB up to the edge (Kaiser designs
        // couple passband ripple to stopband depth; even the 70 dB tier
        // measures ~+/-0.003 dB).
        for (double f = 0.0; f <= p.passband_hz; f += 250.0) {
            EXPECT_NEAR(response_db(h, traits::k_phases, traits::k_input_rate_hz, f), 0.0, 0.01)
                << name << ": passband deviation at " << f << " Hz";
        }

        // Stopband: rated attenuation plus 1 dB margin from the edge out to
        // well past the first several images.
        double worst = -1e9;
        for (double f = traits::k_stopband_edge_hz; f <= 4.0 * traits::k_input_rate_hz; f += 100.0) {
            worst = std::max(worst, response_db(h, traits::k_phases, traits::k_input_rate_hz, f));
        }
        EXPECT_LT(worst, -(p.stopband_atten_db + 1.0)) << name;

        // Branch DC uniformity: the per-branch normalization in
        // design_prototype makes every branch's sum exactly 1.0 in double
        // (machine epsilon), which is what kills the fs_out/L-harmonic spurs
        // a raw windowed-sinc's branch-sum spread would inject from DC/LF
        // energy, and what row-sum quantization then preserves in fixed point.
        double lo = 1e9, hi = -1e9;
        for (std::size_t ph = 0; ph < traits::k_phases; ++ph) {
            double sum = 0.0;
            for (std::size_t t = 0; t < p.taps<D>(); ++t) {
                sum += h[t * traits::k_phases + ph];
            }
            lo = std::min(lo, sum);
            hi = std::max(hi, sum);
        }
        EXPECT_NEAR(lo, 1.0, 1e-12) << name;
        EXPECT_NEAR(hi, 1.0, 1e-12) << name;

        std::printf("[ measured ] %-24s L=%3zu taps=%3zu  worst stopband %7.2f dB  storage(f32) %5.1f KiB\n", name,
                    traits::k_phases, p.taps<D>(), worst,
                    static_cast<double>(h.size() * sizeof(float)) / 1024.0);
    }

    TEST(Design, DownEconomyMeetsSpec) {
        check_meets_spec<direction::down_to_44k1>(profile::economy(), "down economy");
    }
    TEST(Design, DownTransparentMeetsSpec) {
        check_meets_spec<direction::down_to_44k1>(profile::transparent(), "down transparent");
    }
    TEST(Design, UpEconomyMeetsSpec) {
        check_meets_spec<direction::up_to_48k>(profile::economy(), "up economy");
    }
    TEST(Design, UpTransparentMeetsSpec) {
        check_meets_spec<direction::up_to_48k>(profile::transparent(), "up transparent");
    }

    // The direction asymmetry the handoff doc predicted: 48->44.1 carries the
    // sharp 22.05 kHz-forced transition and costs roughly twice the cheap
    // direction. Sharing one transposed prototype would pay that 2x in the
    // cheap direction for nothing — pinned here so nobody "simplifies" it.
    TEST(Design, DirectionsAreAsymmetric) {
        const profile eco = profile::economy();
        EXPECT_GE(eco.taps_down_to_44k1, (eco.taps_up_to_48k * 3) / 2);
        const profile tr = profile::transparent();
        EXPECT_GE(tr.taps_down_to_44k1, (tr.taps_up_to_48k * 3) / 2);
    }

    TEST(Design, BadProfilesThrow) {
        profile p        = profile::economy();
        p.passband_hz    = 23000.0; // above the down direction's stopband edge
        EXPECT_THROW((design_prototype<direction::down_to_44k1>(p)), std::invalid_argument);
        profile q            = profile::economy();
        q.stopband_atten_db  = -1.0;
        EXPECT_THROW((design_prototype<direction::up_to_48k>(q)), std::invalid_argument);
    }

} // namespace
