// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0 (x4x8 plan 6.1): the real FFT's cost for the low-band round
// trip, host wall clock, indicative only. DspTap's tap::dsp::real_fft
// (double, the srdif engine) and real_fft32 (float), forward() then
// inverse() (out of place, the inverse's 2/N included), one pair per hop at
// overlap 4 (hop N / 4), so the cost per full-rate sample is
//   pair time * 4 / N            for an N-point FFT at the host rate, and
//   pair time * 4 / (4096 * D)   for a 4096-point FFT after decimating by D.
// Windowing and the process itself are not included (the same work either
// way, per bin). Each size is timed in 15 runs of enough pairs to take
// about 0.2 s each; the table gives the fastest run (the least disturbed on
// a shared host, and what the per-sample columns use) and the median. Build
// with ../build.sh (-O3 -DNDEBUG, CMake's Release).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "tap/dsp/fft.h"

namespace {

    struct timing {
        double fastest;
        double median;
    };

    template <typename Fft, typename S>
    timing pair_seconds(std::size_t n) {
        Fft            fft(n);
        std::vector<S> x(n), spec(n), y(n);
        for (std::size_t i = 0; i < n; ++i) {
            x[i] = static_cast<S>(std::sin(0.001 * static_cast<double>(i * i % 7919)));
        }
        // Calibrate the repetition count to about 0.2 s.
        std::size_t reps = 1;
        for (;;) {
            const auto t0 = std::chrono::steady_clock::now();
            for (std::size_t r = 0; r < reps; ++r) {
                fft.forward(x.data(), spec.data());
                fft.inverse(spec.data(), y.data());
            }
            const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (dt > 0.2) {
                break;
            }
            reps *= 2;
        }
        std::vector<double> runs;
        double              sink = 0.0;
        for (int k = 0; k < 15; ++k) {
            const auto t0 = std::chrono::steady_clock::now();
            for (std::size_t r = 0; r < reps; ++r) {
                fft.forward(x.data(), spec.data());
                fft.inverse(spec.data(), y.data());
                sink += static_cast<double>(y[r % n]);
            }
            runs.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()
                           / static_cast<double>(reps));
        }
        std::sort(runs.begin(), runs.end());
        if (sink == 12345.678) {
            std::printf("%g\n", sink); // keeps the loop
        }
        return {runs.front(), runs[runs.size() / 2]};
    }

    template <typename Fft, typename S>
    void table(const char* name) {
        std::printf("\n%s: forward + inverse per hop at overlap 4\n", name);
        std::printf("| points | pair, fastest (us) | pair, median (us) | per full-rate sample at the host rate (us) |\n"
                    "|---:|---:|---:|---:|\n");
        double t4096 = 0.0;
        for (const std::size_t n : {4096u, 16384u, 32768u, 65536u}) {
            const timing t = pair_seconds<Fft, S>(n);
            if (n == 4096) {
                t4096 = t.fastest;
            }
            std::printf("| %zu | %.2f | %.2f | %.4f |\n", n, t.fastest * 1e6, t.median * 1e6,
                        t.fastest * 4.0 / static_cast<double>(n) * 1e6);
        }
        std::printf("4096 points after decimating by D, per full-rate sample (us, fastest):");
        for (const unsigned d : {4u, 8u, 16u}) {
            std::printf("  D = %u: %.5f", d, t4096 * 4.0 / (4096.0 * d) * 1e6);
        }
        std::printf("\n");
    }

} // namespace

int main() {
    table<tap::dsp::real_fft, double>("real_fft (double)");
    table<tap::dsp::real_fft32, float>("real_fft32 (float)");
}
