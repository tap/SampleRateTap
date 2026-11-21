// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// A 4096-point FFT with 0.73 Hz bins in the low band of a 48 kHz signal:
// down by 16 to 3 kHz with down_by16, a spectral change there, and the
// change alone interpolated back with up_by16 and added to the input
// delayed by the round trip's exact latency T (docs/X4_X8_PLAN.md 6.3, 6.6):
//
//   out[n] = x[n - T] + up_by16(change(down_by16(x)))[n],  T = 16 tau_down + tau_up.
//
// Only the change goes through the chains, so the bins the process leaves
// alone, and everything above the low band, reach the output untouched: no
// aliasing is re-injected and no crossover filter is designed. The aliases
// name the matrix's chains by 16 at economy (down2_down4_down2 and
// up2_up4_up2); nothing is looked up from a rate (D12). Offline, on whole
// buffers, so the structure is plain to read; tests/test_low_band.cpp pins
// its claims, streamed in blocks, at x4, x8 and x16 and every profile.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include "tap/dsp/fft.h"
#include "tap/dsp/math.h"
#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational;   // NOLINT(google-build-using-namespace)
    using down16 = down_by16<double, 1>; // 48 -> 3 kHz
    using up16   = up_by16<double, 1>;   //  3 -> 48 kHz

    constexpr double      k_fs     = 48000.0;
    constexpr double      k_fs_low = 3000.0;
    constexpr std::size_t k_n      = 4096;    // the FFT, at either rate
    constexpr std::size_t k_hop    = k_n / 4; // the STFT: periodic Hann, overlap 4
    constexpr double      k_pi     = std::numbers::pi;

    /// |X[k]| of a periodic-Hann frame of x from `at`, in DspTap's packing.
    std::vector<double> magnitudes(const std::vector<double>& x, std::size_t at) {
        tap::dsp::real_fft  fft(k_n);
        std::vector<double> buf(k_n);
        std::vector<double> mag(k_n / 2 + 1);
        for (std::size_t i = 0; i < k_n; ++i) {
            buf[i] = x[at + i] * tap::dsp::periodic_hann(i, k_n);
        }
        fft.forward_inplace(buf.data());
        mag[0]       = std::fabs(buf[0]);
        mag[k_n / 2] = std::fabs(buf[1]);
        for (std::size_t k = 1; k < k_n / 2; ++k) {
            mag[k] = std::hypot(buf[2 * k], buf[2 * k + 1]);
        }
        return mag;
    }

    /// The bins from lo to hi Hz, in dB below the frame's peak, as bars.
    void bars(const char* title, const std::vector<double>& mag, double bin_hz, double lo, double hi) {
        std::printf("%s (bins of %.3f Hz)\n", title, bin_hz);
        const double peak = *std::max_element(mag.begin(), mag.end());
        for (auto k = static_cast<std::size_t>(lo / bin_hz); k <= static_cast<std::size_t>(hi / bin_hz) + 1; ++k) {
            const double db  = tap::dsp::amplitude_db(mag[k] / peak + 1e-12);
            const int    len = std::max(0, static_cast<int>((db + 60.0) / 2.0));
            std::printf("  %8.2f Hz %6.1f dB %s\n", static_cast<double>(k) * bin_hz, db,
                        std::string(static_cast<std::size_t>(len), '#').c_str());
        }
    }

    /// The change a spectral process makes, at the low rate: minus the bins
    /// within four of 205 Hz in each frame, nothing elsewhere (an empty
    /// change when `identity`), overlap-added with the same window over its
    /// gain at overlap 4, 1.5.
    std::vector<double> change(const std::vector<double>& low, bool identity) {
        std::vector<double> delta(low.size(), 0.0);
        tap::dsp::real_fft  fft(k_n);
        const auto          k0 = static_cast<std::size_t>(std::lround(205.0 / (k_fs_low / static_cast<double>(k_n))));
        std::vector<double> buf(k_n);
        std::vector<double> d(k_n);
        for (std::size_t at = 0; at + k_n <= low.size(); at += k_hop) {
            for (std::size_t i = 0; i < k_n; ++i) {
                buf[i] = low[at + i] * tap::dsp::periodic_hann(i, k_n);
            }
            fft.forward_inplace(buf.data());
            std::fill(d.begin(), d.end(), 0.0);
            for (std::size_t k = k0 - 4; k <= k0 + 4 && !identity; ++k) {
                d[2 * k]     = -buf[2 * k];
                d[2 * k + 1] = -buf[2 * k + 1];
            }
            fft.inverse(d.data(), d.data());
            for (std::size_t i = 0; i < k_n; ++i) {
                delta[at + i] += d[i] * tap::dsp::periodic_hann(i, k_n) / 1.5;
            }
        }
        return delta;
    }

    /// The amplitude of the tone at f in x over [at, at + len): one bin of
    /// a rectangular DFT, exact when len holds a whole number of its cycles.
    double amplitude(const std::vector<double>& x, std::size_t at, std::size_t len, double f) {
        std::complex<double> sum{0.0, 0.0};
        for (std::size_t n = 0; n < len; ++n) {
            sum += x[at + n] * std::polar(1.0, -2.0 * k_pi * f * static_cast<double>(n) / k_fs);
        }
        return 2.0 * std::abs(sum) / static_cast<double>(len);
    }

} // namespace

int main() {
    // 12 s at 48 kHz: 200 Hz and 205 Hz at -12 dBFS, 5 Hz apart, and 5 kHz
    // at -20 dBFS, above the low band.
    const std::size_t   len = 12 * 48000;
    std::vector<double> x(len);
    for (std::size_t i = 0; i < len; ++i) {
        const double t = static_cast<double>(i) / k_fs;
        x[i]           = 0.25 * std::sin(2.0 * k_pi * 200.0 * t) + 0.25 * std::sin(2.0 * k_pi * 205.0 * t)
               + 0.1 * std::sin(2.0 * k_pi * 5000.0 * t);
    }

    // 1. Resolution: 4096 points at 48 kHz (11.7 Hz bins) against 4096 at
    //    3 kHz after down by 16 (0.73 Hz bins).
    bars("4096-point FFT at 48 kHz: one peak", magnitudes(x, 96000), k_fs / static_cast<double>(k_n), 170.0, 235.0);
    down16              down; // economy: 70 dB, flat to 3/8 of 3 kHz
    std::vector<double> low(down.outputs_for(len));
    low.resize(down.process(x.data(), len, low.data()));
    bars("4096-point FFT at 3 kHz, after down_by16: two peaks", magnitudes(low, 6000),
         k_fs_low / static_cast<double>(k_n), 194.0, 211.0);

    // 2. The round trip's delay, exact: the decimator's latency in low-rate
    //    frames times 16, plus the interpolator's in full-rate samples.
    up16                        up;
    const tap::dsp::exact_ratio t_exact =
        down.latency_output_frames() * tap::dsp::exact_ratio{16, 1} + up.latency_output_frames();
    const std::size_t delay = t_exact.num / t_exact.den; // an integer for every alias
    std::printf(
        "\nround trip: T = 16 x %.4f + %.0f = %zu full-rate samples (%.3f ms); %.4f MACs per full-rate sample\n",
        down.latency_output_frames().value(), up.latency_output_frames().value(), delay,
        static_cast<double>(delay) / 48.0, down.macs_per_output() / 16.0 + up.macs_per_output());

    // 3. The change alone through up_by16, added to the input delayed by T.
    //    Levels over 2.8 s from 4 s in (9600 samples hold a whole number of
    //    cycles of each tone, so one bin of the DFT measures each alone).
    const std::size_t at   = 4 * 48000;
    const std::size_t span = 9600 * 14;
    for (const bool identity : {true, false}) {
        up16                      u;
        const std::vector<double> delta = change(low, identity);
        std::vector<double>       back(u.outputs_for(delta.size()));
        back.resize(u.process(delta.data(), delta.size(), back.data()));
        for (const std::size_t d : {delay, delay - 1, delay + 1}) {
            std::vector<double> out(back.size());
            for (std::size_t n = 0; n < out.size(); ++n) {
                out[n] = (n >= d ? x[n - d] : 0.0) + back[n];
            }
            if (identity) {
                if (d != delay) {
                    continue;
                }
                bool same = true;
                for (std::size_t n = d; n < out.size(); ++n) {
                    same = same && out[n] == x[n - d];
                }
                std::printf("identity change: out[n] == x[n - T] for every n: %s\n", same ? "yes, bit for bit" : "no");
                continue;
            }
            std::printf("remove 205 Hz at the low rate, delay %s (level change, input -> output):", d == delay ? "T    "
                                                                                                    : d < delay
                                                                                                        ? "T - 1"
                                                                                                        : "T + 1");
            for (const double f : {205.0, 200.0, 5000.0}) {
                std::printf("  %4.0f Hz %+7.2f dB", f,
                            tap::dsp::amplitude_db(amplitude(out, at + d, span, f) / amplitude(x, at, span, f)));
            }
            std::printf("\n");
        }
    }
    return 0;
}
