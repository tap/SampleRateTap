// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0 (x4x8 plan, finding dsp-1): the pinned taps per branch m of an
// L-th-band design under a chosen rule for the designer's Kaiser beta, on
// the shipping criterion (worst stopband on the 16384-point grid at or below
// -(A + 1) dB, design.h's k_design_grid_points and k_design_margin_db).
//
// Today's designer takes beta = kaiser_beta(A) (rational/include/tap/sr/
// rational/design.h:301 designs with it, DspTap's search_nyquist_m,
// include/tap/dsp/nyquist.h:214, searches with it). The rules here:
//   dA=<x>  beta = kaiser_beta(A + x) for every design (x = 0 is today's);
//   search  beta searched per design beside m: the smallest m for which some
//           beta = kaiser_beta(A + x), x on the grid -5, -4.75, ..., +25 dB,
//           meets the spec; the beta pinned is the one with the deepest
//           worst stopband at that m.
//
// Reads keys "band p_num p_den A" (one per line) on stdin, writes
// "band p_num p_den A m x worst_db" per key, in input order. Uses DspTap's
// own design_nyquist / nyquist_response_db / nyquist_worst_stopband_db, so
// rule dA=0 reproduces search_nyquist_m(band, p, A, 1.0, 128, 16384)
// exactly. A coarse pass over every 16th point of the same grid rejects a
// candidate before the full grid is swept: a subset of the grid, so it never
// rejects a design the full grid accepts.
//
// Build: see ../build.sh. Run: generator/x0_matrix.py drives it.
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "tap/dsp/kaiser.h"
#include "tap/dsp/nyquist.h"

namespace {

    constexpr std::size_t k_grid   = 16384;
    constexpr double      k_margin = 1.0;
    constexpr std::size_t k_max_m  = 128;

    struct key {
        std::size_t band;
        long long   p_num;
        long long   p_den;
        double      atten;
        std::size_t m     = 0;
        double      x     = 0.0;
        double      worst = 0.0;
    };

    /// The response at grid point g of nyquist_worst_stopband_db's grid.
    double at_grid(const std::vector<double>& h, std::size_t band, double p, std::size_t g) {
        const double f_stop = (1.0 - p) / static_cast<double>(band);
        const double f      = f_stop + (0.5 - f_stop) * static_cast<double>(g) / static_cast<double>(k_grid - 1);
        return tap::dsp::nyquist_response_db(h, band, f);
    }

    /// True when the coarse subset of the grid already exceeds the target.
    bool coarse_fails(const std::vector<double>& h, std::size_t band, double p, double target) {
        for (std::size_t g = 0; g < k_grid; g += 16) {
            if (at_grid(h, band, p, g) > target) {
                return true;
            }
        }
        return at_grid(h, band, p, k_grid - 1) > target;
    }

    /// The worst stopband of the design at (band, m, beta), or +inf when the
    /// coarse pass already fails the target.
    double worst_of(std::size_t band, std::size_t m, double beta, double p, double target) {
        std::vector<double> h(tap::dsp::nyquist_length(band, m));
        tap::dsp::design_nyquist(h, band, beta);
        if (coarse_fails(h, band, p, target)) {
            return 1e9;
        }
        return tap::dsp::nyquist_worst_stopband_db(h, band, p, k_grid);
    }

    void solve(key& k, bool search, double dA) {
        const double        p      = static_cast<double>(k.p_num) / static_cast<double>(k.p_den);
        const double        target = -(k.atten + k_margin);
        std::vector<double> xs;
        if (search) {
            for (int i = -20; i <= 100; ++i) {
                xs.push_back(0.25 * i);
            }
        }
        else {
            xs.push_back(dA);
        }
        for (std::size_t m = 1; m <= k_max_m; ++m) {
            bool   found = false;
            double best  = 1e9;
            double bestx = 0.0;
            for (const double x : xs) {
                const double w = worst_of(k.band, m, tap::dsp::kaiser_beta(k.atten + x), p, target);
                if (w <= target) {
                    found = true;
                    if (w < best) {
                        best  = w;
                        bestx = x;
                    }
                }
            }
            if (found) {
                k.m     = m;
                k.x     = bestx;
                k.worst = best;
                return;
            }
        }
        k.m = 0;
    }

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: pins_beta dA=<x>|search < keys\n");
        return 2;
    }
    const bool       search = std::strcmp(argv[1], "search") == 0;
    const double     dA     = search ? 0.0 : std::atof(argv[1] + 3);
    std::vector<key> keys;
    key              k{};
    while (std::cin >> k.band >> k.p_num >> k.p_den >> k.atten) {
        keys.push_back(k);
    }
    std::atomic<std::size_t> next{0};
    const unsigned           n_threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < n_threads; ++t) {
        pool.emplace_back([&] {
            for (std::size_t i = next++; i < keys.size(); i = next++) {
                solve(keys[i], search, dA);
            }
        });
    }
    for (auto& th : pool) {
        th.join();
    }
    for (const auto& kk : keys) {
        std::printf("%zu %lld %lld %g %zu %g %.4f\n", kk.band, kk.p_num, kk.p_den, kk.atten, kk.m, kk.x, kk.worst);
    }
}
