// Program-weighted quality: the metric that makes FilterSpec::economy()'s
// promise testable, and the evidence that the k*fs transmission zeros do
// what the design says (see the book's epilogue chapter and
// notebooks/asrc_rbj_analysis.ipynb).
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "srt/asrc.hpp"
#include "support/multitone_analysis.hpp"
#include "support/sine_analysis.hpp"
#include "support/two_clock_sim.hpp"

namespace {

constexpr double kFs = 48000.0;
constexpr double kEps = 200e-6;

// ANCHOR: pw_measure
// 24 pink-weighted tones, 60 Hz - 16 kHz, through a +200 ppm offset; the
// residual after removing every tone is everything the converter got wrong,
// weighted the way real program material weights it.
double measureProgramSnrDb(const srt::FilterSpec& spec) {
    srt::Config cfg;
    cfg.channels = 1;
    cfg.filter = spec;
    srt::AsyncSampleRateConverter asrc(cfg);
    const double fsIn = kFs * (1.0 + kEps);
    srt_test::TwoClockSim sim{
        .asrc = asrc, .fsIn = fsIn, .fsOut = kFs, .channels = 1, .chunkIn = 1, .chunkOut = 1};
    const auto comb = srt_test::ToneComb::pink(24, 60.0, 16000.0, 0.9);
    sim.gen = [&](std::uint64_t i) { return static_cast<float>(comb.sampleAt(i, fsIn)); };
    std::vector<float> tail;
    tail.reserve(48000);
    const double total = 40.0; // Quiet-stage settling, as in the sine suite
    sim.run(total, [&](const float* x, std::size_t frames, double t) {
        if (t >= total - 1.0)
            tail.insert(tail.end(), x, x + frames);
    });
    EXPECT_EQ(asrc.status().underruns, 0u);
    EXPECT_EQ(asrc.status().state, srt::State::Locked);
    const double snr = srt_test::programWeightedSnrDb(tail, comb, fsIn, kFs);
    std::printf("[ measured ] program-weighted (24 pink tones), %zu phases x %zu taps: %.1f dB\n",
                spec.numPhases, spec.tapsPerPhase, snr);
    return snr;
}
// ANCHOR_END: pw_measure

// Worst-case single sine near Nyquist, for the honesty line in economy()'s
// documentation: this preset trades exactly this number.
double measureSineSnrDb(const srt::FilterSpec& spec, double freqHz) {
    srt::Config cfg;
    cfg.channels = 1;
    cfg.filter = spec;
    srt::AsyncSampleRateConverter asrc(cfg);
    srt_test::TwoClockSim sim{.asrc = asrc,
                              .fsIn = kFs * (1.0 + kEps),
                              .fsOut = kFs,
                              .channels = 1,
                              .chunkIn = 1,
                              .chunkOut = 1};
    const double nuIn = freqHz / kFs;
    sim.gen = [&](std::uint64_t i) {
        return static_cast<float>(0.5 *
                                  std::sin(2.0 * std::numbers::pi * nuIn * static_cast<double>(i)));
    };
    std::vector<float> tail;
    const double total = 40.0;
    sim.run(total, [&](const float* x, std::size_t frames, double t) {
        if (t >= total - 1.0)
            tail.insert(tail.end(), x, x + frames);
    });
    const auto fit = srt_test::fitSineTracked(tail, nuIn * (1.0 + kEps));
    const double snr = srt_test::snrDb(fit);
    std::printf("[ measured ] economy %5.0f Hz sine: %.1f dB\n", freqHz, snr);
    return snr;
}

// The instrument itself must not be the floor: a synthetic tail of exact
// tones (with a deliberate 0.137 ppm ratio offset, mimicking servo
// settling residue) must measure at the double-precision fit floor.
TEST(ProgramWeighted, InstrumentFloor) {
    const auto comb = srt_test::ToneComb::pink(24, 60.0, 16000.0, 0.9);
    const double rho = 1.0 + 0.137e-6;
    std::vector<float> tail(48000);
    for (std::size_t i = 0; i < tail.size(); ++i) {
        double v = 0.0;
        for (std::size_t k = 0; k < comb.freqHz.size(); ++k)
            v += comb.amplitude[k] * std::sin(2.0 * std::numbers::pi * comb.freqHz[k] * rho / kFs *
                                                  static_cast<double>(i) +
                                              comb.phase[k]);
        tail[i] = static_cast<float>(v);
    }
    const double snr = srt_test::programWeightedSnrDb(tail, comb, kFs * (1.0 + kEps), kFs);
    std::printf("[ measured ] instrument floor (synthetic exact tones): %.1f dB\n", snr);
    // float storage of the tail quantizes at ~ -150 dB; the fit must reach
    // it (measured 151.9 dB).
    EXPECT_GT(snr, 145.0);
}

// Thresholds pinned 4-7 dB under first measurement, per suite convention.
// The claim under test: economy() (2/3 the taps of balanced) stays within a
// few dB of balanced() on PROGRAM-weighted material, because its k*fs zeros
// hold the images of the energetic bottom octaves at balanced-class depth —
// while its worst-case sine near Nyquist honestly reads ~96 dB-class.
TEST(ProgramWeighted, BalancedBaseline) {
    // Measured 134.5 dB.
    EXPECT_GT(measureProgramSnrDb(srt::FilterSpec::balanced()), 128.0);
}
TEST(ProgramWeighted, EconomyNearBalanced) {
    // Measured 131.6 dB — 2.9 dB under balanced at 2/3 the per-sample
    // compute. This single number is the preset's reason to exist.
    const double eco = measureProgramSnrDb(srt::FilterSpec::economy());
    EXPECT_GT(eco, 125.0);
}
TEST(ProgramWeighted, EconomyWorstCaseSineIsDocumented) {
    // Measured 77.4 dB: the deliberate trade, kept visible. (The docs say
    // "96 dB-class"; the extra gap to 77 dB at 19.5 kHz is the L=512
    // interpolation floor at 0.40625 of the sample rate plus the design's
    // transition starting at 18 kHz.)
    EXPECT_GT(measureSineSnrDb(srt::FilterSpec::economy(), 19500.0), 70.0);
}

} // namespace
