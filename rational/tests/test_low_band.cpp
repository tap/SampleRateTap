// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The low-band round trip (docs/X4_X8_PLAN.md 6.3 and 6.6, the use case the
// chains by 4, 8 and 16 were named for; examples/low_band_fft.cpp shows it on
// whole buffers): decimate by D with down_by<D>, make a change at the low
// rate with a 4096-point STFT (periodic Hann, overlap 4) that removes a band
// of bins, interpolate the change alone back with up_by<D>, and add it to the
// input delayed by T:
//
//   out[n] = x[n - T] + up(change(down(x)))[n],  T = D tau_down + tau_up,
//
// tau_down the decimator's latency in low-rate frames and tau_up the
// interpolator's in full-rate samples, an integer for every alias. What it
// pins: an identity change gives the input delayed by T bit for bit (the
// change is exactly zero and the interpolator turns zeros into zeros: a
// structural guarantee); removing a tone reaches the round trip's floor with
// T and fails with T - 1 or T + 1 (the test that pins T against the signal
// path, since the identity holds for any delay); the 4096-point FFT after
// down by 16 resolves two tones 5 Hz apart that the same FFT at 48 kHz
// cannot; and removing one tone leaves the others.
//
// The thresholds are the round trip's, one per tier, the same for FIR and
// IIR stages (the IIR plan's question 5): a removal at least 60 dB deep at
// the 70 dB profiles and 100 dB at transparent, where the chain by 16 is
// down_by16_transparent / up_by16_transparent. The removed tone sits on a bin
// of the low-rate FFT, so the change removes all of it and what is left is
// the round trip's ripple; a tone between bins leaves the Hann window's
// tails outside the removed bins too (the example's 205 Hz: -70.3 dB at
// economy, -94.4 dB at transparent, the mask's floor and not the round
// trip's).
//
// Everything streams in blocks: the input from a sum of two-term recursions
// and the delayed input from a second, identical one (the same sequence, so
// bit for bit the first delayed), every level measured by one bin of a DFT
// (Goertzel's recursion) over a window of a whole number of every tone's
// cycles. A case holds well under 1 MB even by 16 in double (6.6 budgets
// about 1 MB), so the battery runs whole on the M33, M55 and Hexagon legs.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "tap/dsp/fft.h"
#include "tap/dsp/math.h"
#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)
    using tap::dsp::exact_ratio;

    constexpr double      k_fs    = 48000.0;
    constexpr std::size_t k_frame = 4096; ///< the low-rate FFT
    constexpr std::size_t k_hop   = 1024; ///< overlap 4
    constexpr std::size_t k_block = 256;  ///< full-rate frames per input block, low-rate frames per output block
    /// The tone grid: a whole number of cycles in 65536 full-rate samples, so
    /// a 65536-sample rectangular window measures each tone without leakage.
    /// Tone k of the grid is a bin of the low-rate FFT at x16, and at x8 and
    /// x4 when k is a multiple of 2 and of 4.
    constexpr double      k_grid      = k_fs / 65536.0;
    constexpr std::size_t k_grid_span = 65536;
    /// The tone removed: 205.078125 Hz, bin 280 of the grid, a bin of the
    /// low-rate FFT at every factor (70, 140 and 280).
    constexpr std::size_t k_removed = 280;

    struct tone {
        double hz;
        double amplitude;
    };

    /// A sum of sines, each by the two-term recursion s[n + 1] = 2 cos(w)
    /// s[n] - s[n - 1] in double: the same sequence every time one is made,
    /// so a second generator is the first delayed, bit for bit, at two
    /// operations per tone and sample (the M33 runs this in soft double).
    class generator {
      public:
        explicit generator(const std::vector<tone>& tones) {
            for (const tone& t : tones) {
                const double w = 2.0 * std::numbers::pi * t.hz / k_fs;
                m_osc.push_back({t.amplitude, 2.0 * std::cos(w), 0.0, -std::sin(w)}); // s[0] = 0, s[-1] = -sin w
            }
        }
        double next() noexcept {
            double v = 0.0;
            for (oscillator& o : m_osc) {
                v += o.amplitude * o.now;
                const double after = o.twice_cos * o.now - o.before;
                o.before           = o.now;
                o.now              = after;
            }
            return v;
        }

      private:
        struct oscillator {
            double amplitude;
            double twice_cos;
            double now;
            double before;
        };
        std::vector<oscillator> m_osc;
    };

    /// One bin of a rectangular DFT by Goertzel's recursion: the amplitude of
    /// the tone at hz in the samples pushed, exact when they hold a whole
    /// number of its cycles.
    class goertzel {
      public:
        explicit goertzel(double hz)
            : m_twice_cos(2.0 * std::cos(2.0 * std::numbers::pi * hz / k_fs)) {}
        void push(double x) noexcept {
            const double q = x + m_twice_cos * m_q1 - m_q2;
            m_q2           = m_q1;
            m_q1           = q;
            ++m_n;
        }
        double amplitude() const noexcept {
            const double power = m_q1 * m_q1 + m_q2 * m_q2 - m_twice_cos * m_q1 * m_q2;
            return 2.0 * std::sqrt(std::max(power, 0.0)) / static_cast<double>(m_n);
        }

      private:
        double      m_twice_cos;
        double      m_q1 = 0.0;
        double      m_q2 = 0.0;
        std::size_t m_n  = 0;
    };

    /// The change at the low rate: a 4096-point STFT, periodic Hann at
    /// overlap 4, whose change spectrum is minus the bins [lo, hi] of each
    /// frame (nothing when lo > hi: the identity), resynthesized with the
    /// same window over its overlap-add gain, 1.5. Streams: push() one
    /// low-rate frame at a time; each hop of complete change frames is
    /// handed to `emit`.
    template <typename S>
    class low_rate_change {
      public:
        low_rate_change(std::size_t lo, std::size_t hi)
            : m_fft(k_frame)
            , m_window(k_frame)
            , m_low(k_frame)
            , m_spectrum(k_frame)
            , m_sum(k_frame, S(0))
            , m_lo(lo)
            , m_hi(hi) {
            for (std::size_t i = 0; i < k_frame; ++i) {
                m_window[i] = static_cast<S>(tap::dsp::periodic_hann(i, k_frame));
            }
        }

        template <typename Emit>
        void push(S v, Emit&& emit) {
            m_low[m_filled++] = v;
            if (m_filled < k_frame) {
                return;
            }
            for (std::size_t i = 0; i < k_frame; ++i) {
                m_spectrum[i] = m_low[i] * m_window[i];
            }
            m_fft.forward_inplace(m_spectrum.data());
            // DspTap's packing: bin k at [2k, 2k + 1] for 1 <= k < N/2.
            for (std::size_t k = 0; k < k_frame / 2; ++k) {
                const bool removed    = k >= m_lo && k <= m_hi;
                m_spectrum[2 * k]     = removed ? -m_spectrum[2 * k] : S(0);
                m_spectrum[2 * k + 1] = removed ? -m_spectrum[2 * k + 1] : S(0);
            }
            m_fft.inverse(m_spectrum.data(), m_spectrum.data());
            for (std::size_t i = 0; i < k_frame; ++i) {
                m_sum[i] += m_spectrum[i] * m_window[i] / S(1.5);
            }
            emit(m_sum.data(), k_hop);
            std::copy(m_sum.begin() + k_hop, m_sum.end(), m_sum.begin());
            std::fill(m_sum.end() - k_hop, m_sum.end(), S(0));
            std::copy(m_low.begin() + k_hop, m_low.end(), m_low.begin());
            m_filled = k_frame - k_hop;
        }

      private:
        tap::dsp::basic_real_fft<S> m_fft;
        std::vector<S>              m_window;
        std::vector<S>              m_low;
        std::vector<S>              m_spectrum;
        std::vector<S>              m_sum;
        std::size_t                 m_lo;
        std::size_t                 m_hi;
        std::size_t                 m_filled = 0;
    };

    /// What a round trip measured: T, whether the output was the delayed
    /// input bit for bit at every sample, and each tone's level in the
    /// delayed input and in the output with the delay T - 1, T and T + 1.
    struct round_trip_result {
        exact_ratio                        t;
        bool                               bit_for_bit = true;
        std::vector<double>                in_level;
        std::array<std::vector<double>, 3> out_level; ///< delays T - 1, T, T + 1
        double                             removal_db(std::size_t delay, std::size_t i) const {
            return tap::dsp::amplitude_db(out_level[delay][i] / in_level[i]);
        }
    };

    /// The round trip through the chains Down and Up at profile p, the
    /// change removing the low-rate bins [lo, hi], each tone measured over
    /// `span` full-rate samples once every stage and every frame of the
    /// change is in its steady state (nothing measured for the identity,
    /// which checks every sample).
    template <typename Down, typename Up>
    round_trip_result round_trip(const profile& p, const std::vector<tone>& tones, std::size_t lo, std::size_t hi,
                                 std::size_t span, bool measure = true) {
        using sample                   = typename Down::sample;
        constexpr std::size_t k_factor = Down::k_down;
        static_assert(Down::k_up == 1 && Up::k_up == k_factor && Up::k_down == 1);
        Down              down(p);
        Up                up(p);
        round_trip_result r;
        r.t = down.latency_output_frames() * exact_ratio{k_factor, 1} + up.latency_output_frames();
        EXPECT_EQ(r.t.den, 1u) << "T is an integer for every alias";
        const std::size_t t = r.t.num;
        // Steady from the fourth frame of the change on (its overlap-add
        // complete) plus both chains' and the delay's settling.
        const std::size_t start = (3 * k_hop + 64) * k_factor + 2 * t;
        const std::size_t end   = start + span;

        generator               input(tones);
        generator               delayed(tones); // x[n - T + 1] at output n
        std::array<sample, 3>   x_back{};       // x[n - T + 1], x[n - T], x[n - T - 1]
        low_rate_change<sample> change(lo, hi);
        std::vector<sample>     in(k_block);
        std::vector<sample>     low(k_block / k_factor + 4);
        std::vector<sample>     back(k_block * k_factor + 64);
        // Per measured tone: the delayed input, then the output at T - 1, T
        // and T + 1.
        std::vector<std::array<goertzel, 4>> meters;
        for (std::size_t i = 0; i < (measure ? tones.size() : 0); ++i) {
            const double hz = tones[i].hz;
            meters.push_back({goertzel(hz), goertzel(hz), goertzel(hz), goertzel(hz)});
        }
        std::size_t n    = 0; // the output index
        const auto  take = [&](const sample* u, std::size_t m) {
            for (std::size_t j = 0; j < m && n < end; ++j, ++n) {
                x_back[2]      = x_back[1];
                x_back[1]      = x_back[0];
                x_back[0]      = n + 1 >= t ? static_cast<sample>(delayed.next()) : sample(0);
                const sample y = static_cast<sample>(x_back[1] + u[j]);
                if (std::bit_cast<std::conditional_t<sizeof(sample) == 8, std::uint64_t, std::uint32_t>>(y)
                    != std::bit_cast<std::conditional_t<sizeof(sample) == 8, std::uint64_t, std::uint32_t>>(
                        x_back[1])) {
                    r.bit_for_bit = false;
                }
                if (n >= start) {
                    for (std::array<goertzel, 4>& meter : meters) {
                        meter[0].push(static_cast<double>(x_back[1]));
                        for (std::size_t d = 0; d < 3; ++d) {
                            meter[d + 1].push(static_cast<double>(static_cast<sample>(x_back[d] + u[j])));
                        }
                    }
                }
            }
        };
        const auto emit = [&](const sample* delta, std::size_t m) {
            for (std::size_t at = 0; at < m; at += k_block) {
                const std::size_t chunk = std::min(k_block, m - at);
                take(back.data(), up.process(delta + at, chunk, back.data()));
            }
        };
        while (n < end) {
            for (sample& v : in) {
                v = static_cast<sample>(input.next());
            }
            const std::size_t made = down.process(in.data(), k_block, low.data());
            for (std::size_t j = 0; j < made; ++j) {
                change.push(low[j], emit);
            }
        }
        for (const std::array<goertzel, 4>& m : meters) {
            r.in_level.push_back(m[0].amplitude());
            for (std::size_t d = 0; d < 3; ++d) {
                r.out_level[d].push_back(m[d + 1].amplitude());
            }
        }
        return r;
    }

    /// The bin of tone k of the grid in the low-rate FFT after down by D.
    constexpr std::size_t low_bin(std::size_t grid_k, std::size_t factor) {
        return grid_k * factor / 16;
    }

    const std::array<profile, 4> k_profiles = {profile::super_economy(), profile::economy(), profile::balanced(),
                                               profile::transparent()};
    constexpr std::array<const char*, 4> k_profile_names = {"super_economy", "economy", "balanced", "transparent"};
    /// The removal's floor per profile: the 70 dB tier's, then the 120 dB
    /// tier's.
    double removal_floor_db(std::size_t prof) {
        return prof == 3 ? -100.0 : -60.0;
    }

    /// The round trip by D at profile index prof: down_by<D> / up_by<D>, by
    /// 16 at transparent the _transparent pair, the chain of its tier.
    template <typename S, std::size_t D>
    round_trip_result round_trip_by(std::size_t prof, const std::vector<tone>& tones, std::size_t lo, std::size_t hi,
                                    std::size_t span, bool measure = true) {
        if constexpr (D == 4) {
            return round_trip<down_by4<S, 1>, up_by4<S, 1>>(k_profiles[prof], tones, lo, hi, span, measure);
        }
        else if constexpr (D == 8) {
            return round_trip<down_by8<S, 1>, up_by8<S, 1>>(k_profiles[prof], tones, lo, hi, span, measure);
        }
        else if (prof == 3) {
            return round_trip<down_by16_transparent<S, 1>, up_by16_transparent<S, 1>>(k_profiles[prof], tones, lo, hi,
                                                                                      span, measure);
        }
        else {
            return round_trip<down_by16<S, 1>, up_by16<S, 1>>(k_profiles[prof], tones, lo, hi, span, measure);
        }
    }

    /// The demonstration's three tones (6.6): 200 and 205 Hz at -12 dBFS, and
    /// 5 kHz at -20 dBFS, above every low band. A window of 9600 samples
    /// holds a whole number of cycles of each.
    const std::vector<tone> k_demo      = {{205.0, 0.25}, {200.0, 0.25}, {5000.0, 0.1}};
    constexpr std::size_t   k_demo_span = 9600 * 7;

    // ------------------------------------------------------------------

    template <typename S, std::size_t D>
    void expect_identity() {
        for (std::size_t prof = 0; prof < 4; ++prof) {
            const round_trip_result r = round_trip_by<S, D>(prof, k_demo, 1, 0, k_demo_span, false);
            EXPECT_TRUE(r.bit_for_bit) << "by " << D << " at " << k_profile_names[prof] << " in "
                                       << (sizeof(S) == 8 ? "double" : "float");
        }
    }

    TEST(LowBand, IdentityChangeIsTheInputDelayedBitForBit) {
        expect_identity<float, 4>();
        expect_identity<float, 8>();
        expect_identity<float, 16>();
        expect_identity<double, 4>();
        expect_identity<double, 8>();
        expect_identity<double, 16>();
    }

    /// T in full-rate samples: D tau_down + tau_up, the aliases' latencies
    /// at economy (47 / 4 and 47 by 4, 91 / 8 and 91 by 8, 189 / 16 and 189
    /// by 16) and by 16 at transparent (455 / 16 and 455).
    template <typename Down, typename Up>
    exact_ratio round_trip_delay(const profile& p) {
        return Down(p).latency_output_frames() * exact_ratio{Down::k_down, 1} + Up(p).latency_output_frames();
    }

    TEST(LowBand, RoundTripDelayIsTheExactSumOfTheChains) {
        EXPECT_EQ((round_trip_delay<down_by4<float, 1>, up_by4<float, 1>>(profile::economy())), (exact_ratio{94, 1}));
        EXPECT_EQ((round_trip_delay<down_by8<float, 1>, up_by8<float, 1>>(profile::economy())), (exact_ratio{182, 1}));
        EXPECT_EQ((round_trip_delay<down_by16<float, 1>, up_by16<float, 1>>(profile::economy())),
                  (exact_ratio{378, 1}));
        EXPECT_EQ(
            (round_trip_delay<down_by16_transparent<float, 1>, up_by16_transparent<float, 1>>(profile::transparent())),
            (exact_ratio{910, 1}));
    }

    template <typename S, std::size_t D>
    void expect_removal_needs_the_exact_delay() {
        const std::vector<tone> tones = {{k_removed * k_grid, 0.25}};
        const std::size_t       bin   = low_bin(k_removed, D);
        for (std::size_t prof = 0; prof < 4; ++prof) {
            const round_trip_result r     = round_trip_by<S, D>(prof, tones, bin - 4, bin + 4, k_grid_span);
            const auto              where = [&] {
                return ::testing::Message() << "by " << D << " at " << k_profile_names[prof] << " in "
                                            << (sizeof(S) == 8 ? "double" : "float");
            };
            EXPECT_LE(r.removal_db(1, 0), removal_floor_db(prof)) << where() << ", T = " << r.t.num;
            // One sample early or late, the two paths no longer cancel: what
            // is left is 2 sin(pi f / fs) of the tone, -31.4 dB at 205 Hz.
            EXPECT_GT(r.removal_db(0, 0), -40.0) << where() << ", T - 1";
            EXPECT_GT(r.removal_db(2, 0), -40.0) << where() << ", T + 1";
            EXPECT_NEAR(r.removal_db(0, 0), -31.42, 0.01) << where();
            EXPECT_NEAR(r.removal_db(2, 0), -31.42, 0.01) << where();
        }
    }

    TEST(LowBand, RemovalNeedsTheExactDelay) {
        expect_removal_needs_the_exact_delay<float, 4>();
        expect_removal_needs_the_exact_delay<float, 8>();
        expect_removal_needs_the_exact_delay<float, 16>();
        expect_removal_needs_the_exact_delay<double, 4>();
        expect_removal_needs_the_exact_delay<double, 8>();
        expect_removal_needs_the_exact_delay<double, 16>();
    }

    /// The magnitude of a 4096-point Hann frame's bins from `first` to
    /// `last`, in dB below the largest of them.
    template <typename S>
    std::vector<double> frame_db(const std::vector<S>& x, std::size_t first, std::size_t last) {
        tap::dsp::basic_real_fft<S> fft(k_frame);
        std::vector<S>              f(k_frame);
        for (std::size_t i = 0; i < k_frame; ++i) {
            f[i] = x[i] * static_cast<S>(tap::dsp::periodic_hann(i, k_frame));
        }
        fft.forward_inplace(f.data());
        std::vector<double> mag;
        for (std::size_t k = first; k <= last; ++k) {
            mag.push_back(std::hypot(static_cast<double>(f[2 * k]), static_cast<double>(f[2 * k + 1])));
        }
        const double peak = *std::max_element(mag.begin(), mag.end());
        for (double& m : mag) {
            m = tap::dsp::amplitude_db(m / peak);
        }
        return mag;
    }

    /// The dip between the two highest local maxima of `db`, below the
    /// lower of them, or 0 when it has one maximum.
    double dip_db(const std::vector<double>& db) {
        std::vector<std::size_t> peaks;
        for (std::size_t k = 1; k + 1 < db.size(); ++k) {
            if (db[k] > db[k - 1] && db[k] >= db[k + 1]) {
                peaks.push_back(k);
            }
        }
        if (peaks.size() < 2) {
            return 0.0;
        }
        std::sort(peaks.begin(), peaks.end(), [&](std::size_t a, std::size_t b) { return db[a] > db[b]; });
        const std::size_t a      = std::min(peaks[0], peaks[1]);
        const std::size_t b      = std::max(peaks[0], peaks[1]);
        const double      valley = *std::min_element(db.begin() + static_cast<std::ptrdiff_t>(a),
                                                     db.begin() + static_cast<std::ptrdiff_t>(b) + 1);
        return std::min(db[a], db[b]) - valley;
    }

    /// The demonstration's tones through down by 16 at profile prof: the
    /// dip between 200 and 205 Hz in a 4096-point frame at 3 kHz (0.73 Hz
    /// bins), from low-rate frame 6000, 2 s in.
    template <typename S, typename Down>
    double dip_after_down16(const profile& p) {
        Down                  down(p);
        generator             input(k_demo);
        std::vector<S>        in(k_block);
        std::vector<S>        low(k_block / 16 + 4);
        std::vector<S>        frame;
        constexpr std::size_t k_at = 6000;
        std::size_t           seen = 0;
        while (frame.size() < k_frame) {
            for (S& v : in) {
                v = static_cast<S>(input.next());
            }
            const std::size_t made = down.process(in.data(), k_block, low.data());
            for (std::size_t j = 0; j < made && frame.size() < k_frame; ++j, ++seen) {
                if (seen >= k_at) {
                    frame.push_back(low[j]);
                }
            }
        }
        // 200 Hz is bin 273.07 and 205 Hz bin 279.89 of 0.732 Hz.
        return dip_db(frame_db(frame, 266, 287));
    }

    template <typename S>
    void expect_resolution() {
        for (std::size_t prof = 0; prof < 4; ++prof) {
            const double dip = prof == 3 ? dip_after_down16<S, down_by16_transparent<S, 1>>(k_profiles[prof])
                                         : dip_after_down16<S, down_by16<S, 1>>(k_profiles[prof]);
            EXPECT_GE(dip, 40.0) << k_profile_names[prof] << " in " << (sizeof(S) == 8 ? "double" : "float");
        }
        // The same 4096 points at 48 kHz: 11.7 Hz bins, the two tones 0.43
        // bins apart, one peak.
        generator      input(k_demo);
        std::vector<S> full(k_frame);
        for (std::size_t i = 0; i < 96000; ++i) {
            input.next();
        }
        for (S& v : full) {
            v = static_cast<S>(input.next());
        }
        EXPECT_EQ(dip_db(frame_db(full, 12, 24)), 0.0) << "one peak at 48 kHz";
    }

    TEST(LowBand, ResolvesTonesFiveHertzApartAfterDown16) {
        expect_resolution<float>();
        expect_resolution<double>();
    }

    /// Removing the tone on bin 280 of the grid with two others present: one
    /// in the low band seven bins of the low-rate FFT below it (5.13 Hz by
    /// 16, 10.25 by 8, 20.5 by 4) and one near 5 kHz, above every low band.
    template <typename S, std::size_t D>
    void expect_others_left() {
        const std::vector<tone> tones = {
            {k_removed * k_grid, 0.25}, {(k_removed - 7 * 16 / D) * k_grid, 0.25}, {6827 * k_grid, 0.1}};
        const std::size_t bin = low_bin(k_removed, D);
        for (std::size_t prof = 0; prof < 4; ++prof) {
            const round_trip_result r     = round_trip_by<S, D>(prof, tones, bin - 4, bin + 4, k_grid_span);
            const auto              where = [&] {
                return ::testing::Message() << "by " << D << " at " << k_profile_names[prof] << " in "
                                            << (sizeof(S) == 8 ? "double" : "float");
            };
            EXPECT_LE(r.removal_db(1, 0), removal_floor_db(prof)) << where();
            EXPECT_NEAR(r.removal_db(1, 1), 0.0, 0.01) << where() << ", the tone 7 bins below";
            EXPECT_NEAR(r.removal_db(1, 2), 0.0, 0.01) << where() << ", the tone near 5 kHz";
        }
    }

    TEST(LowBand, RemovingOneToneLeavesTheOthers) {
        expect_others_left<float, 4>();
        expect_others_left<float, 8>();
        expect_others_left<float, 16>();
        expect_others_left<double, 4>();
        expect_others_left<double, 8>();
        expect_others_left<double, 16>();
        // The demonstration's tones by 16, 205 Hz between bins: the 70 dB
        // tier's floor, and 200 Hz and 5 kHz unchanged.
        for (std::size_t prof = 0; prof < 3; ++prof) {
            const round_trip_result r = round_trip_by<double, 16>(prof, k_demo, 276, 284, k_demo_span);
            EXPECT_LE(r.removal_db(1, 0), -60.0) << k_profile_names[prof];
            EXPECT_NEAR(r.removal_db(1, 1), 0.0, 0.01) << k_profile_names[prof];
            EXPECT_NEAR(r.removal_db(1, 2), 0.0, 0.01) << k_profile_names[prof];
        }
    }

} // namespace
