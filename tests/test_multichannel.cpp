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

#include "srt/asrc.h"
#include "support/sine_analysis.h"
#include "support/two_clock_sim.h"

namespace {

    constexpr double k_fs  = 48000.0;
    constexpr double k_eps = 200e-6;
    constexpr double k_amp = 0.4;

    /// Distinct, non-harmonically-related tone per channel, all inside the flat
    /// passband for up to 16 channels (max 600 + 731*15 = 11565 Hz).
    double channel_freq_hz(std::size_t c) {
        return 600.0 + 731.0 * static_cast<double>(c);
    }

    template <typename S>
    S make_sample(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return srt::detail::round_sat<S>(v * static_cast<double>(std::numeric_limits<S>::max()));
        }
    }

    template <typename S>
    double to_float_norm(S v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<double>(v);
        }
        else {
            return static_cast<double>(v) / (static_cast<double>(std::numeric_limits<S>::max()) + 1.0);
        }
    }

    struct channel_report {
        double amplitude          = 0.0;
        double snr_db             = 0.0;
        double worst_crosstalk_db = -300.0; ///< worst other-channel tone, dB rel. own
    };

    /// Runs `channels` distinct tones through one converter across a +200 ppm
    /// clock crossing, then analyzes the last `windowSeconds` per channel.
    /// chunk = 1 reproduces the quality-suite methodology (sample-granular
    /// transfer reaches the Quiet servo stage); chunk = 8 is AVB Class A-like
    /// granularity for the Track-stage short variant.
    template <typename S>
    std::vector<channel_report> measure_independence(std::size_t channels, double total_seconds, double window_seconds,
                                                     std::size_t chunk) {
        srt::config cfg;
        cfg.channels = channels;
        srt::basic_async_sample_rate_converter<S> asrc(cfg);
        srt_test::two_clock_sim_t<S>              sim{.asrc      = asrc,
                                                      .fs_in     = k_fs * (1.0 + k_eps),
                                                      .fs_out    = k_fs,
                                                      .channels  = channels,
                                                      .chunk_in  = chunk,
                                                      .chunk_out = chunk};
        sim.gen_ch = [&](std::uint64_t i, std::size_t c) {
            const double w = 2.0 * std::numbers::pi * channel_freq_hz(c) / k_fs;
            // Per-channel phase offsets decorrelate the channel waveforms.
            return make_sample<S>(k_amp * std::sin(w * static_cast<double>(i) + 0.7 * static_cast<double>(c)));
        };

        std::vector<S> tail;
        tail.reserve(static_cast<std::size_t>(window_seconds * k_fs + 16.0) * channels);
        sim.run(total_seconds, [&](const S* x, std::size_t frames, double t) {
            if (t >= total_seconds - window_seconds) {
                tail.insert(tail.end(), x, x + frames * channels);
            }
        });
        EXPECT_EQ(asrc.status().underruns, 0u);
        EXPECT_EQ(asrc.status().state, srt::converter_state::locked);

        const std::size_t           frames = tail.size() / channels;
        std::vector<float>          x(frames);
        std::vector<channel_report> reports(channels);
        for (std::size_t c = 0; c < channels; ++c) {
            for (std::size_t f = 0; f < frames; ++f) {
                x[f] = static_cast<float>(to_float_norm(tail[f * channels + c]));
            }

            // Own tone: tracked fit, then exact removal of the fitted component.
            const double nu_own  = channel_freq_hz(c) / k_fs * (1.0 + k_eps);
            const auto   own     = srt_test::fit_sine_tracked(x, nu_own);
            reports[c].amplitude = own.amplitude;
            reports[c].snr_db    = srt_test::snr_db(own);
            const double w_own   = 2.0 * std::numbers::pi * own.freq_norm;
            const double a       = own.amplitude * std::cos(own.phase);
            const double b       = own.amplitude * std::sin(own.phase);
            for (std::size_t f = 0; f < frames; ++f) {
                const double ph = w_own * static_cast<double>(f);
                x[f] -= static_cast<float>(a * std::sin(ph) + b * std::cos(ph) + own.dc);
            }

            for (std::size_t k = 0; k < channels; ++k) {
                if (k == c) {
                    continue;
                }
                const double nu_k = channel_freq_hz(k) / k_fs * (1.0 + k_eps);
                const auto   leak = srt_test::fit_sine(x, nu_k);
                const double db   = 20.0 * std::log10(leak.amplitude / own.amplitude);
                if (db > reports[c].worst_crosstalk_db) {
                    reports[c].worst_crosstalk_db = db;
                }
            }
            std::printf("[ measured ] ch %2zu (%5.0f Hz): amp %.4f, SNR %6.1f dB, "
                        "worst crosstalk %7.1f dB\n",
                        c, channel_freq_hz(c), reports[c].amplitude, reports[c].snr_db, reports[c].worst_crosstalk_db);
        }
        return reports;
    }

    // Host-grade runs: long enough for the Quiet servo stage, quality-grade
    // thresholds (float floor here is interpolation noise at the per-channel
    // tone frequency; Q15's is the format's own quantization).
    TEST(MultiChannel, Independence12chFloat) {
        const auto r = measure_independence<float>(12, 40.0, 1.0, 1);
        for (const auto& ch : r) {
            EXPECT_NEAR(ch.amplitude, k_amp, 0.01);
            EXPECT_GT(ch.snr_db, 100.0);
            EXPECT_LT(ch.worst_crosstalk_db, -100.0);
        }
    }

    TEST(MultiChannel, Independence16chQ15) {
        const auto r = measure_independence<std::int16_t>(16, 40.0, 1.0, 1);
        for (const auto& ch : r) {
            EXPECT_NEAR(ch.amplitude, k_amp, 0.01);
            EXPECT_GT(ch.snr_db, 72.0);
            EXPECT_LT(ch.worst_crosstalk_db, -72.0);
        }
    }

    // Emulation-sized variant (runs in the bare-metal suite, where the long
    // MultiChannel.* runs are excluded): a Track-stage run that still catches
    // any channel permutation or gross crosstalk on the target's own datapath
    // — including the wide-MAC dotRow paths (SMLALD on M33-class).
    // Channels 5 and 7 are the only counts that reach the channel-parallel
    // K=2 and K=1 remainder tiles (8/4/2/1 tiling: 5 = 4+1, 7 = 4+2+1) — the
    // audit found those tiles had zero coverage. Float, because float is the
    // channel-parallel sample type.
    TEST(MultiChannelShort, Independence5chFloat) {
        const auto r = measure_independence<float>(5, 4.0, 0.25, 8);
        for (const auto& ch : r) {
            EXPECT_NEAR(ch.amplitude, k_amp, 0.05);
            EXPECT_GT(ch.snr_db, 35.0);
            EXPECT_LT(ch.worst_crosstalk_db, -50.0);
        }
    }

    TEST(MultiChannelShort, Independence7chFloat) {
        const auto r = measure_independence<float>(7, 4.0, 0.25, 8);
        for (const auto& ch : r) {
            EXPECT_NEAR(ch.amplitude, k_amp, 0.05);
            EXPECT_GT(ch.snr_db, 35.0);
            EXPECT_LT(ch.worst_crosstalk_db, -50.0);
        }
    }

    TEST(MultiChannelShort, Independence12chQ15) {
        const auto r = measure_independence<std::int16_t>(12, 4.0, 0.25, 8);
        for (const auto& ch : r) {
            EXPECT_NEAR(ch.amplitude, k_amp, 0.05);
            EXPECT_GT(ch.snr_db, 35.0);
            EXPECT_LT(ch.worst_crosstalk_db, -45.0);
        }
    }

} // namespace
