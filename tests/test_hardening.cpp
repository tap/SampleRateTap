// Regression tests from the package audit: the pull-block feasibility
// adaptation, hardened Config validation, resync accounting, consumer
// reset, degenerate call sizes, fixed-point fade-in — plus QuickQuality,
// an emulation-sized end-to-end SNR/saturation gate that (by name) runs
// on the bare-metal and Hexagon CI legs, which exclude the long quality
// suites and previously had no end-to-end SNR coverage at all.
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "srt/asrc.h"
#include "support/sine_analysis.h"
#include "support/two_clock_sim.h"

namespace {

    constexpr double k_fs = 48000.0;

    // Audit finding F1: with defaults, any pull block larger than the 48-frame
    // setpoint used to drain into a permanent underrun limit cycle (64-frame
    // callbacks dropped out every ~0.24 s forever). The converter now raises
    // its effective setpoint to the observed block; these runs must lock with
    // zero underruns and report the raise.
    void run_feasibility(std::size_t pull_block) {
        tap::samplerate::config cfg;
        cfg.channels = 1;
        // Lock-stage promotion gates compare smoothed occupancy error against
        // frame thresholds; with very coarse blocks the block-quantization
        // sawtooth dwarfs the 1-frame default and Acquire->Track never
        // promotes. Follow the servo_config guidance (thresholds sized to the
        // block) for the 240-frame case; the feasibility fix under test is
        // independent of this tuning.
        if (pull_block >= 240) {
            cfg.servo.lock_threshold_frames   = static_cast<double>(pull_block) / 8.0;
            cfg.servo.unlock_threshold_frames = static_cast<double>(pull_block) * 1.5;
        }
        tap::samplerate::async_sample_rate_converter asrc(cfg);
        srt_test::two_clock_sim                      sim{.asrc      = asrc,
                                                         .fs_in     = k_fs * (1.0 + 200e-6),
                                                         .fs_out    = k_fs,
                                                         .channels  = 1,
                                                         .chunk_in  = 32,
                                                         .chunk_out = pull_block};
        sim.gen = [](std::uint64_t i) { return static_cast<float>(0.5 * std::sin(0.13 * static_cast<double>(i))); };
        // Coarse blocks keep the servo in Track, where instantaneous ppm swings
        // with the block-beat FM — average it, as the 48 kHz lock test does.
        double      ppm_sum = 0.0;
        std::size_t blocks  = 0;
        sim.run(20.0, [&](const float*, std::size_t, double t) {
            if (t > 10.0) {
                ppm_sum += asrc.status().ppm;
                ++blocks;
            }
        });
        const auto st = asrc.status();
        EXPECT_EQ(st.state, tap::samplerate::converter_state::locked) << "pull=" << pull_block;
        EXPECT_EQ(st.underruns, 0u) << "pull=" << pull_block;
        EXPECT_GT(st.effective_target_latency_frames, 48u) << "pull=" << pull_block;
        EXPECT_NEAR(ppm_sum / static_cast<double>(blocks), 200.0, 25.0) << "pull=" << pull_block;
    }

    TEST(Feasibility, Pull64LocksCleanly) {
        run_feasibility(64);
    }
    TEST(Feasibility, Pull128LocksCleanly) {
        run_feasibility(128);
    }
    TEST(Feasibility, Pull240LocksCleanly) {
        run_feasibility(240);
    }

    TEST(Feasibility, SmallPullsKeepConfiguredSetpoint) {
        tap::samplerate::config cfg;
        cfg.channels = 1;
        tap::samplerate::async_sample_rate_converter asrc(cfg);
        srt_test::two_clock_sim sim{.asrc = asrc, .fs_in = k_fs * (1.0 + 200e-6), .fs_out = k_fs, .channels = 1};
        sim.run(5.0, [](const float*, std::size_t, double) {});
        // 32-frame pulls against the 48-frame default were always feasible;
        // the adaptation must not inflate latency for them.
        EXPECT_EQ(asrc.status().effective_target_latency_frames, 48u);
    }

    // Audit finding F2: these all constructed successfully and misbehaved
    // silently (NaN coefficient tables, image-passing filters, UB-range eps).
    TEST(ConfigValidation, RejectsSilentMisbehavior) {
        {
            tap::samplerate::config c;
            c.sample_rate_hz = std::numeric_limits<double>::quiet_NaN();
            EXPECT_THROW(tap::samplerate::async_sample_rate_converter{c}, std::invalid_argument);
        }
        {
            tap::samplerate::config c; // anti-image cutoff above input Nyquist
            c.filter.passband_hz = 23000.0;
            c.filter.stopband_hz = 47000.0;
            EXPECT_THROW(tap::samplerate::async_sample_rate_converter{c}, std::invalid_argument);
        }
        {
            tap::samplerate::config c; // eps * 2^64 would overflow int64 in the phase path
            c.servo.max_deviation_ppm = 400000.0;
            EXPECT_THROW(tap::samplerate::async_sample_rate_converter{c}, std::invalid_argument);
        }
        {
            tap::samplerate::config c;
            c.servo.quiet_bandwidth_hz = std::numeric_limits<double>::infinity();
            EXPECT_THROW(tap::samplerate::async_sample_rate_converter{c}, std::invalid_argument);
        }
        {
            tap::samplerate::config c;
            c.fifo_frames = 64; // below the high-watermark capacity requirement
            EXPECT_THROW(tap::samplerate::async_sample_rate_converter{c}, std::invalid_argument);
        }
        // The rate-scaling factory sits exactly on the band-edge sum boundary
        // (passband + stopband == fs up to rounding); it must keep constructing.
        EXPECT_NO_THROW(
            tap::samplerate::async_sample_rate_converter{tap::samplerate::config::for_sample_rate(16000.0)});
        EXPECT_NO_THROW(
            tap::samplerate::async_sample_rate_converter{tap::samplerate::config::for_sample_rate(44100.0)});
    }

    // Audit finding F3: with a setpoint below the resampler's staged-scratch
    // size (16 frames), a hard resync used to drain the ring entirely and
    // cascade straight back into Filling.
    TEST(Resync, SmallSetpointRecovers) {
        tap::samplerate::config cfg;
        cfg.channels              = 1;
        cfg.target_latency_frames = 4;
        tap::samplerate::async_sample_rate_converter asrc(cfg);
        std::vector<float>                           in(32, 0.25f);
        std::vector<float>                           out(64);
        for (int i = 0; i < 8; ++i) { // reach steady operation
            asrc.push(in.data(), 32), asrc.pull(out.data(), 32);
        }
        for (int i = 0; i < 40; ++i) { // consumer stall: drive occupancy over the watermark
            asrc.push(in.data(), 32);
        }
        std::size_t made_after = 0;
        for (int i = 0; i < 8; ++i) {
            asrc.push(in.data(), 32);
            made_after += asrc.pull(out.data(), 32);
        }
        EXPECT_GE(asrc.status().resyncs, 1u);
        // The old behavior produced 0 frames here (permanent refill cascade).
        EXPECT_GT(made_after, 6u * 32u);
    }

    TEST(Reset, ConsumerResetRelocks) {
        tap::samplerate::config cfg;
        cfg.channels = 1;
        tap::samplerate::async_sample_rate_converter asrc(cfg);
        srt_test::two_clock_sim sim{.asrc = asrc, .fs_in = k_fs * (1.0 + 200e-6), .fs_out = k_fs, .channels = 1};
        sim.run(5.0, [](const float*, std::size_t, double) {});
        ASSERT_EQ(asrc.status().state, tap::samplerate::converter_state::locked);
        asrc.reset_from_consumer();
        EXPECT_EQ(asrc.status().state, tap::samplerate::converter_state::filling);
        srt_test::two_clock_sim sim2{.asrc = asrc, .fs_in = k_fs * (1.0 + 200e-6), .fs_out = k_fs, .channels = 1};
        sim2.run(5.0, [](const float*, std::size_t, double) {});
        EXPECT_EQ(asrc.status().state, tap::samplerate::converter_state::locked);
    }

    TEST(EdgeCalls, ZeroLengthAndOversized) {
        tap::samplerate::config cfg;
        cfg.channels = 2;
        tap::samplerate::async_sample_rate_converter asrc(cfg);
        std::vector<float>                           in(2 * 4096, 0.1f);
        std::vector<float>                           out(2 * 8192);
        EXPECT_EQ(asrc.push(in.data(), 0), 0u);
        EXPECT_EQ(asrc.pull(out.data(), 0), 0u);
        for (int i = 0; i < 64; ++i) {
            asrc.push(in.data(), 32);
        }
        // Oversized pull: bounded behavior — synthesize what the backlog allows,
        // silence-pad the rest, count the underrun; every sample finite.
        const std::size_t made = asrc.pull(out.data(), 8192);
        EXPECT_LE(made, 8192u);
        for (float v : out) {
            ASSERT_TRUE(std::isfinite(v));
        }
    }

    // Fixed-point fade-in: test_fade.cpp covers float only; the Q15 scaleSample
    // branch (round-and-saturate) was untested.
    TEST(FadeQ15, OutputRampsAfterFill) {
        tap::samplerate::config cfg;
        cfg.channels = 1;
        tap::samplerate::async_sample_rate_converter_q15 asrc(cfg);
        std::vector<std::int16_t>                        in(32, 16384);
        std::vector<std::int16_t>                        out(32);
        std::vector<std::int16_t>                        made;
        for (int it = 0; it < 400 && made.size() < 200; ++it) {
            asrc.push(in.data(), in.size());
            const std::size_t n = asrc.pull(out.data(), out.size());
            for (std::size_t k = 0; k < n; ++k) {
                made.push_back(out[k]);
            }
        }
        ASSERT_GE(made.size(), 200u);
        EXPECT_LT(std::abs(made[0]), 3300) << "first frame attenuated";
        for (std::size_t k = 1; k < 64; ++k) {
            EXPECT_GE(made[k] + 1, made[k - 1]) << "monotonic ramp at " << k;
        }
        EXPECT_NEAR(made[80], 16384, 200) << "full level after the ramp";
    }

    // Emulation-sized end-to-end gates (these run on the M33/M55 bare-metal
    // suites and the Hexagon leg, whose exclusion filters keep out every long
    // quality suite — leaving those targets without any on-target SNR check).
    TEST(QuickQuality, Q15Tone997) {
        tap::samplerate::config cfg;
        cfg.channels = 1;
        tap::samplerate::async_sample_rate_converter_q15 asrc(cfg);
        srt_test::two_clock_sim_t<std::int16_t>          sim{
                     .asrc = asrc, .fs_in = k_fs * (1.0 + 200e-6), .fs_out = k_fs, .channels = 1, .chunk_in = 8, .chunk_out = 8};
        const double nu = 997.0 / k_fs;
        sim.gen         = [&](std::uint64_t i) {
            return tap::samplerate::detail::round_sat<std::int16_t>(
                0.5 * 32767.0 * std::sin(2.0 * std::numbers::pi * nu * static_cast<double>(i)));
        };
        std::vector<float> tail;
        sim.run(4.0, [&](const std::int16_t* x, std::size_t frames, double t) {
            if (t >= 3.5) {
                for (std::size_t n = 0; n < frames; ++n) {
                    tail.push_back(static_cast<float>(x[n]) / 32768.0f);
                }
            }
        });
        EXPECT_EQ(asrc.status().underruns, 0u);
        const auto fit = srt_test::fit_sine_tracked(tail, nu * (1.0 + 200e-6));
        // Track-stage run (8-frame blocks, 4 s): block-beat FM dominates the
        // tracked-fit residual at ~40+ dB — far below the Quiet-stage Q15
        // figure, far above any gross datapath regression (saturation,
        // wrong-phase rows land below 10 dB). Same floor as MultiChannelShort.
        EXPECT_GT(srt_test::snr_db(fit), 35.0);
    }

    TEST(QuickQuality, FullScaleQ15Short) {
        // 1 s near-full-scale variant of FixedPoint.FullScaleSineDoesNotWrapQ15,
        // sized for emulation and named so the bare-metal filter keeps it: the
        // wide-MAC (SMLALD) target previously never saw near-full-scale input.
        tap::samplerate::config cfg;
        cfg.channels = 1;
        tap::samplerate::async_sample_rate_converter_q15 asrc(cfg);
        srt_test::two_clock_sim_t<std::int16_t>          sim{
                     .asrc = asrc, .fs_in = k_fs * (1.0 + 500e-6), .fs_out = k_fs, .channels = 1, .chunk_in = 8, .chunk_out = 8};
        const double nu = 1000.0 / k_fs;
        sim.gen         = [&](std::uint64_t i) {
            return tap::samplerate::detail::round_sat<std::int16_t>(
                0.99 * 32767.0 * std::sin(2.0 * std::numbers::pi * nu * static_cast<double>(i)));
        };
        std::vector<double> tail;
        sim.run(1.0, [&](const std::int16_t* x, std::size_t frames, double t) {
            if (t > 0.5) {
                for (std::size_t n = 0; n < frames; ++n) {
                    tail.push_back(static_cast<double>(x[n]) / 32768.0);
                }
            }
        });
        const double omega = 2.0 * std::numbers::pi * nu;
        const double bound = 1.5 * 0.99 * omega * omega + 4.0 / 32768.0;
        for (std::size_t n = 1; n + 1 < tail.size(); ++n) {
            ASSERT_LT(std::abs(tail[n + 1] - 2.0 * tail[n] + tail[n - 1]), bound) << "n=" << n;
        }
    }

} // namespace
