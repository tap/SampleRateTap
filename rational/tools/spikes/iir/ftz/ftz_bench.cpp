// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0: moved from the plan's scratch programs, the code unchanged. The source of
// IIR_PLAN I9's x86-64 block costs (guarded, per-sample flush, subnormal states);
// re-run in this spike, outputs/ftz_host.txt.
// What the scoped flush-to-zero guard costs on x86-64, against the work it
// guards: a stereo two-path IIR half-band decimator (4 one-multiplier allpass
// sections, economy's estimate), one call per block. Host wall clock, median of
// runs; indicative only.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include <xmmintrin.h>

struct guard { // save MXCSR, set FTZ (bit 15) and DAZ (bit 6), restore on exit
    unsigned saved;
    guard() noexcept
        : saved(_mm_getcsr()) {
        _mm_setcsr(saved | 0x8040u);
    }
    ~guard() { _mm_setcsr(saved); }
};

constexpr int k_ch = 2;
struct decimator {
    // path 0: sections a0, a1; path 1: sections a2, a3 (first-order allpass
    // y = a (x - y1) + x1, one multiply each), at the low rate.
    std::array<float, 4>                   a{0.0798f, 0.5453f, 0.2838f, 0.8344f};
    std::array<std::array<float, 8>, k_ch> st{}; // x1, y1 per section
    template <bool Flush>
    static inline float section(float a, float x, float& x1, float& y1) noexcept {
        float y = a * (x - y1) + x1;
        x1      = x;
        if constexpr (Flush) {
            if (std::fabs(y) < 1e-30f)
                y = 0.0f;
        }
        y1 = y;
        return y;
    }
    template <bool Flush>
    void process(const float* in, std::size_t frames_out, float* out) noexcept {
        for (std::size_t f = 0; f < frames_out; ++f) {
            for (int c = 0; c < k_ch; ++c) {
                auto&       s = st[c];
                const float e = in[(2 * f) * k_ch + c], o = in[(2 * f + 1) * k_ch + c];
                float       p0    = section<Flush>(a[0], e, s[0], s[1]);
                p0                = section<Flush>(a[1], p0, s[2], s[3]);
                float p1          = section<Flush>(a[2], o, s[4], s[5]);
                p1                = section<Flush>(a[3], p1, s[6], s[7]);
                out[f * k_ch + c] = 0.5f * (p0 + p1);
            }
        }
    }
};

template <class F>
double median_ns(F&& f, int calls) {
    std::vector<double> runs;
    for (int r = 0; r < 15; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < calls; ++i)
            f();
        auto t1 = std::chrono::steady_clock::now();
        runs.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / calls);
    }
    std::sort(runs.begin(), runs.end());
    return runs[runs.size() / 2];
}

int main() {
    volatile float sink = 0;
    // The guard alone, per call.
    const double g = median_ns(
        [&] {
            guard gd;
            sink = sink + 1.0f;
        },
        2000000);
    const double ng = median_ns([&] { sink = sink + 1.0f; }, 2000000);
    std::printf("guard alone: %.1f ns per call (loop without it: %.1f ns)\n", g - ng, ng);
    for (std::size_t block : {32u, 64u, 256u}) {
        std::vector<float> in(2 * block * k_ch), out(block * k_ch);
        for (std::size_t i = 0; i < in.size(); ++i)
            in[i] = std::sin(0.01f * float(i));
        decimator d;
        const int calls = int(4000000 / block);
        auto      plain = median_ns(
            [&] {
                d.process<false>(in.data(), block, out.data());
                sink = out[0];
            },
            calls);
        auto guarded = median_ns(
            [&] {
                guard gd;
                d.process<false>(in.data(), block, out.data());
                sink = out[0];
            },
            calls);
        auto flushed = median_ns(
            [&] {
                d.process<true>(in.data(), block, out.data());
                sink = out[0];
            },
            calls);
        // Silence after signal, with the states left subnormal (the slow case).
        std::vector<float> zeros(in.size(), 0.0f);
        auto               sub = [&](auto run) {
            for (auto& s : d.st)
                s.fill(1e-39f);
            run();
        };
        auto sub_plain = median_ns(
            [&] {
                sub([&] { d.process<false>(zeros.data(), block, out.data()); });
                sink = out[0];
            },
            calls);
        auto sub_guarded = median_ns(
            [&] {
                sub([&] {
                    guard gd;
                    d.process<false>(zeros.data(), block, out.data());
                });
                sink = out[0];
            },
            calls);
        std::printf(
            "block %3zu out frames, stereo: plain %7.1f ns | guarded %7.1f ns (+%.1f%%) | per-sample flush %7.1f ns (+%.1f%%)"
            " || subnormal states: plain %8.1f ns, guarded %7.1f ns\n",
            block, plain, guarded, 100 * (guarded - plain) / plain, flushed, 100 * (flushed - plain) / plain, sub_plain,
            sub_guarded);
    }
}
