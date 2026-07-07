#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "srt/detail/kaiser.h"
#include "srt/polyphase_filter.h"

namespace {

    using namespace srt::detail;

    TEST(Kaiser, BesselI0ReferenceValues) {
        EXPECT_DOUBLE_EQ(bessel_i0(0.0), 1.0);
        EXPECT_NEAR(bessel_i0(1.0), 1.2660658777520084, 1e-12);
        EXPECT_NEAR(bessel_i0(5.0), 27.239871823604442, 1e-9);
        EXPECT_NEAR(bessel_i0(12.0), 18948.925349296309, 1e-6);
    }

    TEST(Kaiser, BetaReferenceValues) {
        EXPECT_NEAR(kaiser_beta(120.0), 0.1102 * (120.0 - 8.7), 1e-12);
        EXPECT_NEAR(kaiser_beta(40.0), 0.5842 * std::pow(19.0, 0.4) + 0.07886 * 19.0, 1e-12);
        EXPECT_DOUBLE_EQ(kaiser_beta(15.0), 0.0);
    }

    TEST(Kaiser, TapEstimateMatchesHarrisFormula) {
        // 120 dB over a 20->28 kHz transition at 48 kHz: ~47 taps per phase.
        const std::size_t taps = estimate_taps(120.0, 8000.0 / 48000.0);
        EXPECT_GE(taps, 45u);
        EXPECT_LE(taps, 49u);
    }

    // Direct DFT magnitude of the double-precision prototype, normalized so the
    // passband sits at 0 dB. f is in Hz; the prototype rate is L * fs.
    double response_db(const std::vector<double>& h, std::size_t num_phases, double fs, double f) {
        const double         proto_rate = static_cast<double>(num_phases) * fs;
        std::complex<double> acc{0.0, 0.0};
        for (std::size_t m = 0; m < h.size(); ++m) {
            const double ang = -2.0 * std::numbers::pi * f * static_cast<double>(m) / proto_rate;
            acc += h[m] * std::polar(1.0, ang);
        }
        return 20.0 * std::log10(std::abs(acc) / static_cast<double>(num_phases));
    }

    void check_prototype_meets_spec(const srt::filter_spec& spec, double fs) {
        const std::size_t   phases = std::bit_ceil(spec.num_phases);
        const std::size_t   n      = phases * spec.taps_per_phase;
        std::vector<double> h(n);
        const double        cutoff_norm = (spec.passband_hz + spec.stopband_hz) / fs;
        if (spec.image_zeros) {
            design_prototype_compensated(h, phases, cutoff_norm, kaiser_beta(spec.stopband_atten_db),
                                         spec.passband_hz / fs);
        }
        else {
            design_prototype(h, phases, cutoff_norm, kaiser_beta(spec.stopband_atten_db));
        }

        // Passband: flat within +/-0.01 dB up to the passband edge. For the
        // compensated designs this is the claim the droop pre-compensation
        // exists to defend (the raw rect would sag -2.64 dB at 20 kHz).
        for (double f = 0.0; f <= spec.passband_hz; f += 500.0) {
            EXPECT_NEAR(response_db(h, spec.num_phases, fs, f), 0.0, 0.01) << "passband deviation at " << f << " Hz";
        }

        // Stopband: at least the rated attenuation (1 dB grace) from the stopband
        // edge out to well past the first few images.
        for (double f = spec.stopband_hz; f <= 3.0 * fs; f += 250.0) {
            EXPECT_LT(response_db(h, spec.num_phases, fs, f), -(spec.stopband_atten_db - 1.0))
                << "stopband leakage at " << f << " Hz";
        }

        // Transmission zeros at every k*fs: exact in exact arithmetic, so demand
        // far below the rated stopband (double rounding measures ~-300 dB).
        if (spec.image_zeros) {
            for (int k = 1; k <= 3; ++k) {
                EXPECT_LT(response_db(h, spec.num_phases, fs, static_cast<double>(k) * fs), -150.0)
                    << "missing transmission zero at " << k << "*fs";
            }
        }
    }

    TEST(Kaiser, FastPrototypeMeetsSpec) {
        check_prototype_meets_spec(srt::filter_spec::fast(), 48000.0);
    }

    TEST(Kaiser, BalancedPrototypeMeetsSpec) {
        check_prototype_meets_spec(srt::filter_spec::balanced(), 48000.0);
    }

    TEST(Kaiser, TransparentPrototypeMeetsSpec) {
        check_prototype_meets_spec(srt::filter_spec::transparent(), 48000.0);
    }

    TEST(Kaiser, EconomyPrototypeMeetsSpec) {
        check_prototype_meets_spec(srt::filter_spec::economy(), 48000.0);
    }

    // The compensated presets must also hold their specs at scaled rates (the
    // 16 kHz deployment path): normalized design, same numbers.
    TEST(Kaiser, CompensatedSpecsHoldAt16k) {
        check_prototype_meets_spec(srt::filter_spec::balanced().scaled_to(16000.0), 16000.0);
        check_prototype_meets_spec(srt::filter_spec::economy().scaled_to(16000.0), 16000.0);
    }

} // namespace
