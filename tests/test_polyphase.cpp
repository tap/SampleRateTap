#include <cmath>
#include <numbers>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "srt/polyphase_filter.hpp"

namespace {

    constexpr double kFs = 48000.0;

    TEST(Polyphase, DcGainIsUnityAcrossMu) {
        const srt::PolyphaseFilterBank<float>  bank(srt::FilterSpec::balanced(), kFs);
        std::vector<float>                     ones(bank.taps(), 1.0f);
        std::mt19937                           rng(7);
        std::uniform_real_distribution<double> uni(0.0, 1.0);
        for (int i = 0; i < 64; ++i) {
            const double mu = uni(rng);
            EXPECT_NEAR(srt::interpolate(bank, ones.data(), mu), 1.0, 1e-4) << "mu=" << mu;
        }
    }

    TEST(Polyphase, ExtraRowEqualsPhaseZeroAdvancedOneTap) {
        const srt::PolyphaseFilterBank<float> bank(srt::FilterSpec::balanced(), kFs);
        const std::size_t                     L = bank.numPhases();
        const std::size_t                     T = bank.taps();
        // Rows are stored tap-reversed over an oldest-first window, so "advanced
        // by one input sample" means row L shifted one slot toward newer samples:
        // phase(L)[u] == phase(0)[u-1], with the oldest slot of row L zero.
        EXPECT_EQ(bank.phase(L)[0], 0.0f);
        for (std::size_t u = 1; u < T; ++u)
            EXPECT_EQ(bank.phase(L)[u], bank.phase(0)[u - 1]) << "tap " << u;
    }

    // Worst-case fractional-delay error against the analytic sine, swept over mu.
    // The interpolated output at fractional position mu corresponds to input time
    // tau = J - T/2 + mu + 1/(2L) where J is the newest sample index in the window.
    double maxErrorDb(const srt::PolyphaseFilterBank<float>& bank, double freqHz) {
        const double       nu = freqHz / kFs;
        const std::size_t  T  = bank.taps();
        const double       L  = static_cast<double>(bank.numPhases());
        std::vector<float> x(4 * T);
        for (std::size_t k = 0; k < x.size(); ++k)
            x[k] = static_cast<float>(std::sin(2.0 * std::numbers::pi * nu * static_cast<double>(k)));
        double maxErr = 0.0;
        for (std::size_t J = 2 * T; J < 2 * T + 8; ++J) {
            const float* hist = x.data() + J - T + 1;
            for (int i = 0; i < 257; ++i) {
                const double mu       = static_cast<double>(i) / 257.0;
                const double tau      = static_cast<double>(J) - static_cast<double>(T) / 2.0 + mu + 1.0 / (2.0 * L);
                const double expected = std::sin(2.0 * std::numbers::pi * nu * tau);
                const double err      = std::abs(static_cast<double>(srt::interpolate(bank, hist, mu)) - expected);
                maxErr                = std::max(maxErr, err);
            }
        }
        return 20.0 * std::log10(maxErr);
    }

    TEST(Polyphase, FractionalDelayAccuracyBalanced) {
        const srt::PolyphaseFilterBank<float> bank(srt::FilterSpec::balanced(), kFs);
        // Error budget: this absolute-error sweep sees BOTH the inter-phase
        // interpolation floor and the prototype's in-spec passband ripple (a
        // gain deviation of r dB reads here as 20*log10(10^(r/20)-1): the
        // +/-0.01 dB passband CONTRACT corresponds to -58.7 dB, and the
        // compensated designs' measured ripple (one correction pass, sized by
        // the M33 constructor budget) reads -70/-87 dB at the passband edge
        // for balanced/transparent. These gates sit between measured and the
        // contract bound — pinning the contract, not the plain Kaiser's
        // incidental +/-0.0001 dB flatness the original numbers leaned on.
        // Alignment bugs still fail loudly: a half-fine-sample delay error
        // measures -72 dB at 1 kHz alone, 23 dB over that gate.
        EXPECT_LT(maxErrorDb(bank, 997.0), -95.0);
        EXPECT_LT(maxErrorDb(bank, 4000.0), -95.0);
        EXPECT_LT(maxErrorDb(bank, 10000.0), -95.0);
        EXPECT_LT(maxErrorDb(bank, 19000.0), -65.0);
    }

    TEST(Polyphase, FractionalDelayAccuracyTransparent) {
        const srt::PolyphaseFilterBank<float> bank(srt::FilterSpec::transparent(), kFs);
        EXPECT_LT(maxErrorDb(bank, 997.0), -104.0);
        EXPECT_LT(maxErrorDb(bank, 19000.0), -80.0);
    }

    TEST(Polyphase, MuWrapIsContinuousWithWindowShift) {
        // interpolate(hist, mu -> 1) must equal interpolate(hist shifted by one
        // newer sample, mu = 0): the whole-sample slip invariant.
        const srt::PolyphaseFilterBank<float> bank(srt::FilterSpec::balanced(), kFs);
        const std::size_t                     T = bank.taps();
        std::vector<float>                    x(2 * T);
        std::mt19937                          rng(99);
        std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
        for (auto& v : x)
            v = uni(rng);
        const float* histOld = x.data();     // window ending at x[T-1]
        const float* histNew = x.data() + 1; // window ending at x[T]
        const float  atWrap  = srt::interpolate(bank, histOld, 1.0 - 1e-9);
        const float  atZero  = srt::interpolate(bank, histNew, 0.0);
        EXPECT_NEAR(atWrap, atZero, 1e-4);
    }

} // namespace
