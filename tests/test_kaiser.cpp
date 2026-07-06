#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "srt/detail/kaiser.hpp"
#include "srt/polyphase_filter.hpp"

namespace {

    using namespace srt::detail;

    TEST(Kaiser, BesselI0ReferenceValues) {
        EXPECT_DOUBLE_EQ(besselI0(0.0), 1.0);
        EXPECT_NEAR(besselI0(1.0), 1.2660658777520084, 1e-12);
        EXPECT_NEAR(besselI0(5.0), 27.239871823604442, 1e-9);
        EXPECT_NEAR(besselI0(12.0), 18948.925349296309, 1e-6);
    }

    TEST(Kaiser, BetaReferenceValues) {
        EXPECT_NEAR(kaiserBeta(120.0), 0.1102 * (120.0 - 8.7), 1e-12);
        EXPECT_NEAR(kaiserBeta(40.0), 0.5842 * std::pow(19.0, 0.4) + 0.07886 * 19.0, 1e-12);
        EXPECT_DOUBLE_EQ(kaiserBeta(15.0), 0.0);
    }

    TEST(Kaiser, TapEstimateMatchesHarrisFormula) {
        // 120 dB over a 20->28 kHz transition at 48 kHz: ~47 taps per phase.
        const std::size_t taps = estimateTaps(120.0, 8000.0 / 48000.0);
        EXPECT_GE(taps, 45u);
        EXPECT_LE(taps, 49u);
    }

    // Direct DFT magnitude of the double-precision prototype, normalized so the
    // passband sits at 0 dB. f is in Hz; the prototype rate is L * fs.
    double responseDb(const std::vector<double>& h, std::size_t numPhases, double fs, double f) {
        const double         protoRate = static_cast<double>(numPhases) * fs;
        std::complex<double> acc{0.0, 0.0};
        for (std::size_t m = 0; m < h.size(); ++m) {
            const double ang = -2.0 * std::numbers::pi * f * static_cast<double>(m) / protoRate;
            acc += h[m] * std::polar(1.0, ang);
        }
        return 20.0 * std::log10(std::abs(acc) / static_cast<double>(numPhases));
    }

    void checkPrototypeMeetsSpec(const srt::FilterSpec& spec, double fs) {
        const std::size_t   phases = std::bit_ceil(spec.numPhases);
        const std::size_t   n      = phases * spec.tapsPerPhase;
        std::vector<double> h(n);
        const double        cutoffNorm = (spec.passbandHz + spec.stopbandHz) / fs;
        if (spec.imageZeros)
            designPrototypeCompensated(h, phases, cutoffNorm, kaiserBeta(spec.stopbandAttenDb), spec.passbandHz / fs);
        else
            designPrototype(h, phases, cutoffNorm, kaiserBeta(spec.stopbandAttenDb));

        // Passband: flat within +/-0.01 dB up to the passband edge. For the
        // compensated designs this is the claim the droop pre-compensation
        // exists to defend (the raw rect would sag -2.64 dB at 20 kHz).
        for (double f = 0.0; f <= spec.passbandHz; f += 500.0)
            EXPECT_NEAR(responseDb(h, spec.numPhases, fs, f), 0.0, 0.01) << "passband deviation at " << f << " Hz";

        // Stopband: at least the rated attenuation (1 dB grace) from the stopband
        // edge out to well past the first few images.
        for (double f = spec.stopbandHz; f <= 3.0 * fs; f += 250.0)
            EXPECT_LT(responseDb(h, spec.numPhases, fs, f), -(spec.stopbandAttenDb - 1.0))
                << "stopband leakage at " << f << " Hz";

        // Transmission zeros at every k*fs: exact in exact arithmetic, so demand
        // far below the rated stopband (double rounding measures ~-300 dB).
        if (spec.imageZeros) {
            for (int k = 1; k <= 3; ++k)
                EXPECT_LT(responseDb(h, spec.numPhases, fs, static_cast<double>(k) * fs), -150.0)
                    << "missing transmission zero at " << k << "*fs";
        }
    }

    TEST(Kaiser, FastPrototypeMeetsSpec) {
        checkPrototypeMeetsSpec(srt::FilterSpec::fast(), 48000.0);
    }

    TEST(Kaiser, BalancedPrototypeMeetsSpec) {
        checkPrototypeMeetsSpec(srt::FilterSpec::balanced(), 48000.0);
    }

    TEST(Kaiser, TransparentPrototypeMeetsSpec) {
        checkPrototypeMeetsSpec(srt::FilterSpec::transparent(), 48000.0);
    }

    TEST(Kaiser, EconomyPrototypeMeetsSpec) {
        checkPrototypeMeetsSpec(srt::FilterSpec::economy(), 48000.0);
    }

    // The compensated presets must also hold their specs at scaled rates (the
    // 16 kHz deployment path): normalized design, same numbers.
    TEST(Kaiser, CompensatedSpecsHoldAt16k) {
        checkPrototypeMeetsSpec(srt::FilterSpec::balanced().scaledTo(16000.0), 16000.0);
        checkPrototypeMeetsSpec(srt::FilterSpec::economy().scaledTo(16000.0), 16000.0);
    }

} // namespace
