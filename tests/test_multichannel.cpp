// Multichannel independence: every channel of one converter instance gets a
// distinct tone, and after conversion each channel must contain its own tone
// at full quality and nothing measurable of any other channel's. This is the
// test that catches interleave/deinterleave permutation and crosstalk bugs,
// which no single-channel quality metric can see. Deployment shapes covered:
// 12 channels (7.1.4 surround) and 16 (an AVB stream bundling reference
// microphones with the program feed).
//
// Method: own tone is removed by tracked least-squares fit; the other
// channels' frequencies are then fitted on the residual, so the own tone's
// spectral leakage (about -67 dB at these spacings over a 1 s rectangular
// window) cannot masquerade as crosstalk. The fit noise floor on the
// residual is ~43 dB below the residual RMS, far under every threshold.
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "srt/asrc.hpp"
#include "support/sine_analysis.hpp"
#include "support/two_clock_sim.hpp"

namespace {

constexpr double kFs = 48000.0;
constexpr double kEps = 200e-6;
constexpr double kAmp = 0.4;

/// Distinct, non-harmonically-related tone per channel, all inside the flat
/// passband for up to 16 channels (max 600 + 731*15 = 11565 Hz).
double channelFreqHz(std::size_t c) {
    return 600.0 + 731.0 * static_cast<double>(c);
}

template <typename S>
S makeSample(double v) {
    if constexpr (std::is_floating_point_v<S>)
        return static_cast<S>(v);
    else
        return srt::detail::roundSat<S>(v * static_cast<double>(std::numeric_limits<S>::max()));
}

template <typename S>
double toFloatNorm(S v) {
    if constexpr (std::is_floating_point_v<S>)
        return static_cast<double>(v);
    else
        return static_cast<double>(v) / (static_cast<double>(std::numeric_limits<S>::max()) + 1.0);
}

struct ChannelReport {
    double amplitude = 0.0;
    double snrDb = 0.0;
    double worstCrosstalkDb = -300.0; ///< worst other-channel tone, dB rel. own
};

/// Runs `channels` distinct tones through one converter across a +200 ppm
/// clock crossing, then analyzes the last `windowSeconds` per channel.
/// chunk = 1 reproduces the quality-suite methodology (sample-granular
/// transfer reaches the Quiet servo stage); chunk = 8 is AVB Class A-like
/// granularity for the Track-stage short variant.
template <typename S>
std::vector<ChannelReport> measureIndependence(std::size_t channels, double totalSeconds,
                                               double windowSeconds, std::size_t chunk) {
    srt::Config cfg;
    cfg.channels = channels;
    srt::BasicAsyncSampleRateConverter<S> asrc(cfg);
    srt_test::TwoClockSimT<S> sim{.asrc = asrc,
                                  .fsIn = kFs * (1.0 + kEps),
                                  .fsOut = kFs,
                                  .channels = channels,
                                  .chunkIn = chunk,
                                  .chunkOut = chunk};
    sim.genCh = [&](std::uint64_t i, std::size_t c) {
        const double w = 2.0 * std::numbers::pi * channelFreqHz(c) / kFs;
        // Per-channel phase offsets decorrelate the channel waveforms.
        return makeSample<S>(kAmp *
                             std::sin(w * static_cast<double>(i) + 0.7 * static_cast<double>(c)));
    };

    std::vector<S> tail;
    tail.reserve(static_cast<std::size_t>(windowSeconds * kFs + 16.0) * channels);
    sim.run(totalSeconds, [&](const S* x, std::size_t frames, double t) {
        if (t >= totalSeconds - windowSeconds)
            tail.insert(tail.end(), x, x + frames * channels);
    });
    EXPECT_EQ(asrc.status().underruns, 0u);
    EXPECT_EQ(asrc.status().state, srt::State::Locked);

    const std::size_t frames = tail.size() / channels;
    std::vector<float> x(frames);
    std::vector<ChannelReport> reports(channels);
    for (std::size_t c = 0; c < channels; ++c) {
        for (std::size_t f = 0; f < frames; ++f)
            x[f] = static_cast<float>(toFloatNorm(tail[f * channels + c]));

        // Own tone: tracked fit, then exact removal of the fitted component.
        const double nuOwn = channelFreqHz(c) / kFs * (1.0 + kEps);
        const auto own = srt_test::fitSineTracked(x, nuOwn);
        reports[c].amplitude = own.amplitude;
        reports[c].snrDb = srt_test::snrDb(own);
        const double wOwn = 2.0 * std::numbers::pi * own.freqNorm;
        const double a = own.amplitude * std::cos(own.phase);
        const double b = own.amplitude * std::sin(own.phase);
        for (std::size_t f = 0; f < frames; ++f) {
            const double ph = wOwn * static_cast<double>(f);
            x[f] -= static_cast<float>(a * std::sin(ph) + b * std::cos(ph) + own.dc);
        }

        for (std::size_t k = 0; k < channels; ++k) {
            if (k == c)
                continue;
            const double nuK = channelFreqHz(k) / kFs * (1.0 + kEps);
            const auto leak = srt_test::fitSine(x, nuK);
            const double db = 20.0 * std::log10(leak.amplitude / own.amplitude);
            if (db > reports[c].worstCrosstalkDb)
                reports[c].worstCrosstalkDb = db;
        }
        std::printf("[ measured ] ch %2zu (%5.0f Hz): amp %.4f, SNR %6.1f dB, "
                    "worst crosstalk %7.1f dB\n",
                    c, channelFreqHz(c), reports[c].amplitude, reports[c].snrDb,
                    reports[c].worstCrosstalkDb);
    }
    return reports;
}

// Host-grade runs: long enough for the Quiet servo stage, quality-grade
// thresholds (float floor here is interpolation noise at the per-channel
// tone frequency; Q15's is the format's own quantization).
TEST(MultiChannel, Independence12chFloat) {
    const auto r = measureIndependence<float>(12, 40.0, 1.0, 1);
    for (const auto& ch : r) {
        EXPECT_NEAR(ch.amplitude, kAmp, 0.01);
        EXPECT_GT(ch.snrDb, 100.0);
        EXPECT_LT(ch.worstCrosstalkDb, -100.0);
    }
}

TEST(MultiChannel, Independence16chQ15) {
    const auto r = measureIndependence<std::int16_t>(16, 40.0, 1.0, 1);
    for (const auto& ch : r) {
        EXPECT_NEAR(ch.amplitude, kAmp, 0.01);
        EXPECT_GT(ch.snrDb, 72.0);
        EXPECT_LT(ch.worstCrosstalkDb, -72.0);
    }
}

// Emulation-sized variant (runs in the bare-metal suite, where the long
// MultiChannel.* runs are excluded): a Track-stage run that still catches
// any channel permutation or gross crosstalk on the target's own datapath
// — including the wide-MAC dotRow paths (SMLALD on M33-class).
TEST(MultiChannelShort, Independence12chQ15) {
    const auto r = measureIndependence<std::int16_t>(12, 4.0, 0.25, 8);
    for (const auto& ch : r) {
        EXPECT_NEAR(ch.amplitude, kAmp, 0.05);
        EXPECT_GT(ch.snrDb, 35.0);
        EXPECT_LT(ch.worstCrosstalkDb, -45.0);
    }
}

} // namespace
