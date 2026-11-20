// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (x4x8/custom.cpp); see ../README.md.
// Scratch measurement: wider usable low band (custom profiles), and the FFT
// timing the use case trades against (host wall clock, indicative only).
#include <chrono>
#include <cstdio>
#include <vector>

#include <tap/dsp/fft.h>
#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;

template <class T>
void row(const char* name, const profile& p, unsigned D, bool down) {
    T    s(p);
    auto lat  = s.latency_output_frames();
    auto full = down ? lat * tap::dsp::exact_ratio{D, 1} : lat;
    std::printf("  %-26s MAC/full-rate sample %6.2f  latency %6.1f full-rate samples\n", name,
                down ? s.macs_per_output() / D : s.macs_per_output(), full.value());
}
profile custom(double p, double a) {
    profile c;
    c.passband_frac     = p;
    c.stopband_atten_db = a;
    c.taps_per_branch   = {0, 0, 0, 0, 0};
    c.relaxations       = {};
    return c;
}
double fft_us(std::size_t n) {
    tap::dsp::real_fft32 f(n);
    std::vector<float>   buf(n + 2);
    for (std::size_t i = 0; i < n; ++i)
        buf[i] = float(i % 17) * 0.01f;
    const int reps = int(4000000 / n) + 10;
    auto      t0   = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r) {
        f.forward_inplace(buf.data());
        f.inverse_inplace(buf.data());
    }
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(t1 - t0).count() / reps;
}
int main() {
    for (auto [p, a] : {std::pair{0.40, 70.0}, std::pair{0.45, 70.0}, std::pair{0.45, 90.0}}) {
        std::printf("custom p = %.2f, A = %.0f dB\n", p, a);
        const profile c = custom(p, a);
        row<chain<1, down_2, down_2>>("down 2 . down 2", c, 4, true);
        row<chain<1, up_2, up_2>>("up 2 . up 2", c, 4, false);
        row<chain<1, down_2, down_2, down_2>>("down 2 . down 2 . down 2", c, 8, true);
        row<chain<1, up_2, up_2, up_2>>("up 2 . up 2 . up 2", c, 8, false);
        row<chain<1, down_3, ratio_3_4, down_2>>("down 3 . 3/4 . down 2", c, 8, true);
        row<chain<1, up_2, ratio_4_3, up_3>>("up 2 . 4/3 . up 3", c, 8, false);
    }
    for (std::size_t n : {512u, 4096u, 16384u, 32768u})
        std::printf("real_fft32 N = %5zu: forward + inverse %8.1f us\n", n, fft_us(n));
}
