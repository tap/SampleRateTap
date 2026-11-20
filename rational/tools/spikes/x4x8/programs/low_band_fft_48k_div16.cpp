// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (examples/low_band_fft_48k_div16.cpp); see ../README.md.
// The x4/x8/x16 plan's use case, at 48 kHz and down 16: a 4096-point FFT with
// 0.73 Hz bins in the low band, a spectral process there, and the "process the
// change" round trip (plan 6.3): out[n] = x[n - T] + up16(change(down16(x)))[n].
// Today's names: the matrix's 1/16 and 16/1 chains (down_8_down_2, up_2_up_8).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tap/dsp/fft.h>
#include <tap/sr/rational/rational.h>

namespace {
    using namespace tap::sr::rational;
    using down16 = down_8_down_2<double, 1>; // 48 -> 3 kHz
    using up16   = up_2_up_8<double, 1>;     //  3 -> 48 kHz

    constexpr double      k_fs = 48000.0, k_fs_low = 3000.0;
    constexpr std::size_t k_n = 4096, k_hop = k_n / 4; // the STFT: periodic Hann, overlap 4
    constexpr double      k_pi = std::numbers::pi;

    std::vector<double> hann(std::size_t n) {
        std::vector<double> w(n);
        for (std::size_t i = 0; i < n; ++i)
            w[i] = 0.5 - 0.5 * std::cos(2 * k_pi * double(i) / double(n));
        return w;
    }
    // |X[k]| of a Hann-windowed frame starting at `at`, DspTap's packing.
    std::vector<double> magnitudes(const std::vector<double>& x, std::size_t at, std::size_t n) {
        tap::dsp::real_fft  fft(n);
        const auto          w = hann(n);
        std::vector<double> buf(n), mag(n / 2 + 1);
        for (std::size_t i = 0; i < n; ++i)
            buf[i] = x[at + i] * w[i];
        fft.forward_inplace(buf.data());
        mag[0]     = std::fabs(buf[0]);
        mag[n / 2] = std::fabs(buf[1]);
        for (std::size_t k = 1; k < n / 2; ++k)
            mag[k] = std::hypot(buf[2 * k], buf[2 * k + 1]);
        return mag;
    }
    void bars(const char* title, const std::vector<double>& mag, double bin_hz, double lo, double hi) {
        std::printf("%s (bins of %.3f Hz)\n", title, bin_hz);
        const double peak = *std::max_element(mag.begin(), mag.end());
        for (std::size_t k = std::size_t(lo / bin_hz); k <= std::size_t(hi / bin_hz) + 1; ++k) {
            const double db  = 20 * std::log10(mag[k] / peak + 1e-12);
            const int    len = std::max(0, int((db + 60) / 2));
            std::printf("  %8.2f Hz %6.1f dB %s\n", double(k) * bin_hz, db, std::string(std::size_t(len), '#').c_str());
        }
    }
    // The change a spectral process makes, at the low rate: here, remove 205 Hz
    // (zero the bins within 4 bins of it); `identity` makes no change at all.
    std::vector<double> change(const std::vector<double>& low, bool identity) {
        std::vector<double> delta(low.size(), 0.0);
        if (identity)
            return delta;
        tap::dsp::real_fft  fft(k_n);
        const auto          w   = hann(k_n);
        const double        bin = k_fs_low / double(k_n);
        const std::size_t   k0  = std::size_t(std::lround(205.0 / bin));
        std::vector<double> buf(k_n);
        for (std::size_t at = 0; at + k_n <= low.size(); at += k_hop) {
            for (std::size_t i = 0; i < k_n; ++i)
                buf[i] = low[at + i] * w[i];
            fft.forward_inplace(buf.data());
            // The change only: minus the removed bins, zero everywhere else.
            std::vector<double> d(k_n, 0.0);
            for (std::size_t k = k0 - 4; k <= k0 + 4; ++k) {
                d[2 * k]     = -buf[2 * k];
                d[2 * k + 1] = -buf[2 * k + 1];
            }
            fft.inverse(d.data(), d.data());
            for (std::size_t i = 0; i < k_n; ++i)
                delta[at + i] += d[i] * w[i] / 1.5; // Hann^2 at overlap 4 sums to 1.5
        }
        return delta;
    }
    double level_db(const std::vector<double>& x, std::size_t at, double f) {
        const std::size_t n   = 65536;
        const auto        mag = magnitudes(x, at, n);
        const std::size_t k   = std::size_t(std::lround(f / (k_fs / double(n))));
        return 20 * std::log10(*std::max_element(mag.begin() + long(k) - 2, mag.begin() + long(k) + 3));
    }
} // namespace

int main() {
    // 12 s at 48 kHz: 200 Hz and 205 Hz, 5 Hz apart, and 5 kHz above the low band.
    const std::size_t   len = 12 * 48000;
    std::vector<double> x(len);
    for (std::size_t i = 0; i < len; ++i) {
        const double t = double(i) / k_fs;
        x[i]           = 0.25 * std::sin(2 * k_pi * 200 * t) + 0.25 * std::sin(2 * k_pi * 205 * t)
               + 0.1 * std::sin(2 * k_pi * 5000 * t);
    }

    // 1. Resolution: 4096 points at 48 kHz (11.7 Hz bins) against 4096 at 3 kHz.
    bars("4096-point FFT at 48 kHz: one peak", magnitudes(x, 96000, k_n), k_fs / double(k_n), 170, 235);
    down16              down;
    up16                up;
    std::vector<double> low(down.outputs_for(len));
    low.resize(down.process(x.data(), len, low.data()));
    bars("4096-point FFT at 3 kHz, after down 16: two peaks", magnitudes(low, 6000, k_n), k_fs_low / double(k_n), 194,
         211);

    // 2. The round trip, T exact: the decimator's latency in full-rate samples plus the interpolator's.
    const auto   lat_down = down.latency_output_frames(), lat_up = up.latency_output_frames();
    const double t_exact = lat_down.value() * 16 + lat_up.value();
    const auto   delay   = std::size_t(std::lround(t_exact));
    std::printf(
        "\nround trip: T = 16 x %.4f + %.0f = %.4f full-rate samples (%.2f ms); MACs per full-rate sample: %.2f\n",
        lat_down.value(), lat_up.value(), t_exact, t_exact / 48.0, down.macs_per_output() / 16 + up.macs_per_output());
    for (bool identity : {true, false}) {
        down16              d2;
        up16                u2;
        std::vector<double> lo2(d2.outputs_for(len));
        lo2.resize(d2.process(x.data(), len, lo2.data()));
        const auto          delta = change(lo2, identity);
        std::vector<double> back(u2.outputs_for(delta.size()));
        back.resize(u2.process(delta.data(), delta.size(), back.data()));
        std::vector<double> out(back.size());
        for (std::size_t n = 0; n < out.size(); ++n)
            out[n] = (n >= delay ? x[n - delay] : 0.0) + back[n];
        if (identity) {
            double worst = 0;
            for (std::size_t n = delay; n < out.size(); ++n)
                worst = std::max(worst, std::fabs(out[n] - x[n - delay]));
            std::printf("identity process: max |out[n] - x[n - T]| = %g (bit for bit: %s)\n", worst,
                        worst == 0 ? "yes" : "no");
        }
        else {
            const std::size_t at = 4 * 48000; // a steady 1.4 s window, past the start-up
            std::printf("remove 205 Hz at the low rate (level change, input -> output):\n");
            for (double f : {200.0, 205.0, 5000.0})
                std::printf("  %6.0f Hz: %+7.2f dB\n", f, level_db(out, at + delay, f) - level_db(x, at, f));
        }
    }
}
